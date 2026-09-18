/* PoC: Mach semaphore wait/signal as a futex_wait/wake substitute on GNU Hurd. */
#include <mach.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ITERS 2000

static semaphore_t sem;
static volatile long counter = 0;
static double send_ts[ITERS];
static double latencies_us[ITERS];

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void *waiter(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        kern_return_t kr = semaphore_wait(sem);
        double t1 = now_us();
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "semaphore_wait failed at iter %d: %d\n", i, kr);
            exit(1);
        }
        latencies_us[i] = t1 - send_ts[i];
        counter++;
    }
    return NULL;
}

int main(void) {
    kern_return_t kr = semaphore_create(mach_task_self(), &sem, SYNC_POLICY_FIFO, 0);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "semaphore_create failed: %d\n", kr);
        return 1;
    }

    pthread_t th;
    if (pthread_create(&th, NULL, waiter, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    usleep(50000); /* let waiter block first */

    for (int i = 0; i < ITERS; i++) {
        send_ts[i] = now_us();
        kr = semaphore_signal(sem);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "semaphore_signal failed at iter %d: %d\n", i, kr);
            return 1;
        }
        usleep(500);
    }

    pthread_join(th, NULL);

    if (counter != ITERS) {
        printf("RESULT: FAIL missing wakeups: got %ld expected %d\n", counter, ITERS);
        return 1;
    }

    double sum = 0, min = 1e18, max = 0;
    for (int i = 0; i < ITERS; i++) {
        double v = latencies_us[i];
        sum += v;
        if (v < min) min = v;
        if (v > max) max = v;
    }
    printf("RESULT: OK iters=%d avg_us=%.2f min_us=%.2f max_us=%.2f\n",
           ITERS, sum / ITERS, min, max);
    return 0;
}

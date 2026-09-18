/* PoC: GNU Mach's native futex-equivalent, gsync_wait/gsync_wake, on GNU Hurd.
 *
 * gsync_wait(task, addr, val1, val2, msec, flags) atomically checks that the
 * 32-bit word at addr equals val1 and, if so, blocks until a matching
 * gsync_wake on the same address -- i.e. exactly Linux's FUTEX_WAIT/FUTEX_WAKE
 * check-and-block semantics (see <mach/gnumach.h>).
 */
#include <mach.h>
#include <mach/gnumach.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ITERS 2000

static volatile uint32_t futex_word = 0xFFFFFFFFu; /* sentinel: nothing posted yet */
static volatile long woke_count = 0;
static double send_ts[ITERS];
static double latencies_us[ITERS];

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void *waiter(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ITERS; i++) {
        kern_return_t kr = gsync_wait(mach_task_self(),
                                       (vm_address_t)&futex_word,
                                       i, 0, 0, 0);
        double t1 = now_us();
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "gsync_wait failed at iter %u: %d\n", i, kr);
            exit(1);
        }
        latencies_us[i] = t1 - send_ts[i];
        woke_count++;
    }
    return NULL;
}

int main(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, waiter, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    usleep(50000); /* let waiter block on iteration 0 first */

    for (uint32_t i = 0; i < ITERS; i++) {
        send_ts[i] = now_us();
        futex_word = i;
        kern_return_t kr = gsync_wake(mach_task_self(),
                                       (vm_address_t)&futex_word, 0, 0);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "gsync_wake failed at iter %u: %d\n", i, kr);
            return 1;
        }
        usleep(500);
    }

    pthread_join(th, NULL);

    if (woke_count != ITERS) {
        printf("RESULT: FAIL missing wakeups: got %ld expected %d\n", woke_count, ITERS);
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

/* PoC: asynchronous SIGUSR1 delivery latency/reliability on GNU Hurd (no SA_RESTART). */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define ITERS 500

static volatile sig_atomic_t got = 0;
static double send_ts[ITERS];
static double recv_ts[ITERS];
static volatile int idx_recv = 0;
static pid_t target_pid;

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void handler(int sig) {
    (void)sig;
    if (idx_recv < ITERS) {
        recv_ts[idx_recv] = now_us();
        idx_recv++;
    }
    got = 1;
}

static void *sender(void *arg) {
    (void)arg;
    usleep(100000);
    for (int i = 0; i < ITERS; i++) {
        send_ts[i] = now_us();
        if (kill(target_pid, SIGUSR1) != 0) {
            perror("kill");
        }
        usleep(2000);
    }
    return NULL;
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* deliberately no SA_RESTART */
    if (sigaction(SIGUSR1, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    target_pid = getpid();

    pthread_t th;
    if (pthread_create(&th, NULL, sender, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    int eintr_count = 0;
    int slept_count = 0;
    for (int i = 0; i < ITERS; i++) {
        struct timespec req = {0, 50L * 1000 * 1000}; /* 50ms */
        int r = nanosleep(&req, NULL);
        if (r != 0 && errno == EINTR) {
            eintr_count++;
        } else {
            slept_count++;
        }
    }

    pthread_join(th, NULL);

    int matched = idx_recv < ITERS ? idx_recv : ITERS;
    double sum = 0, min = 1e18, max = 0;
    int n = 0;
    for (int i = 0; i < matched; i++) {
        double v = recv_ts[i] - send_ts[i];
        if (v < 0) continue;
        sum += v;
        n++;
        if (v < min) min = v;
        if (v > max) max = v;
    }

    printf("RESULT: handler_fired=%d eintr_in_nanosleep=%d full_sleep=%d matched_pairs=%d avg_us=%.2f min_us=%.2f max_us=%.2f\n",
           idx_recv, eintr_count, slept_count, n,
           n ? sum / n : 0.0, n ? min : 0.0, n ? max : 0.0);

    if (idx_recv < ITERS / 2) {
        printf("RESULT: FAIL too few signals delivered (%d/%d)\n", idx_recv, ITERS);
        return 1;
    }
    return 0;
}

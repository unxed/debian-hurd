/* PoC: does an unset-SA_RESTART signal interrupt a blocking read() with
 * EINTR on GNU Hurd, the way it does on Linux/BSD? sig_poc.c already showed
 * nanosleep() is NOT interrupted; this checks whether that's true of
 * blocking I/O (implemented as a Hurd RPC) too, or specific to nanosleep.
 *
 * Each iteration: block on read() of an empty pipe. A sender thread signals
 * SIGUSR1 shortly after (expected EINTR path). A watchdog thread writes a
 * byte later as a fallback so the iteration always terminates even if the
 * signal does not interrupt the read.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ITERS 20

static pid_t target_pid;

static void handler(int sig) { (void)sig; }

static void *sender(void *arg) {
    (void)arg;
    usleep(20000);
    kill(target_pid, SIGUSR1);
    return NULL;
}

static void *watchdog(void *arg) {
    int wfd = *(int *)arg;
    usleep(150000);
    char b = 'x';
    write(wfd, &b, 1);
    return NULL;
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* deliberately no SA_RESTART */
    sigaction(SIGUSR1, &sa, NULL);

    target_pid = getpid();

    int eintr_count = 0, data_count = 0, other_count = 0;

    for (int i = 0; i < ITERS; i++) {
        int fds[2];
        if (pipe(fds) != 0) {
            perror("pipe");
            return 1;
        }

        pthread_t th_sender, th_watchdog;
        pthread_create(&th_sender, NULL, sender, NULL);
        pthread_create(&th_watchdog, NULL, watchdog, &fds[1]);

        char buf[1];
        errno = 0;
        ssize_t r = read(fds[0], buf, 1);
        if (r < 0 && errno == EINTR) {
            eintr_count++;
        } else if (r == 1) {
            data_count++;
        } else {
            other_count++;
        }

        pthread_join(th_sender, NULL);
        pthread_join(th_watchdog, NULL);
        close(fds[0]);
        close(fds[1]);
    }

    printf("RESULT: read_eintr=%d read_completed_via_watchdog=%d other=%d iters=%d\n",
           eintr_count, data_count, other_count, ITERS);
    return 0;
}

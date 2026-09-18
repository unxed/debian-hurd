/* Does Hurd honor changes to the ucontext made by a SA_SIGINFO handler?
 * Go's asynchronous preemption (SIGURG + sigctxt.pushCall) depends on it.
 *   A: pthread_kill() reaches a thread that spins in user mode
 *   B: handler sets gregs[REG_RAX] -> visible in the interrupted code after return
 *   C: handler sets gregs[REG_RIP] -> execution continues at the new address
 * The handler runs on a sigaltstack like Go's. */
#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

static volatile int handler_ran, done, mode;
static pthread_t target;

static void redirect_target(void)
{
	static const char m[] = "CTX C: RIP modification honored\n";
	write(1, m, sizeof m - 1);
	_exit(0);
}

static void handler(int sig, siginfo_t *si, void *uv)
{
	ucontext_t *uc = uv;
	handler_ran++;
	if (mode == 1)
		uc->uc_mcontext.gregs[REG_RAX] = 0x1234;
	else if (mode == 2)
		uc->uc_mcontext.gregs[REG_RIP] = (greg_t)(uintptr_t)redirect_target;
}

static void *killer(void *arg)
{
	struct timespec ts = {0, 300 * 1000 * 1000};
	int i;
	nanosleep(&ts, NULL);
	int r = pthread_kill(target, SIGURG);
	printf("CTX mode %d: pthread_kill rc=%d\n", mode, r);
	fflush(stdout);
	for (i = 0; i < 30 && !done; i++) {
		struct timespec s = {0, 100 * 1000 * 1000};
		nanosleep(&s, NULL);
	}
	if (!done) {
		printf("CTX mode %d: NOT honored (handler_ran=%d)\n", mode, handler_ran);
		fflush(stdout);
		_exit(10 + mode);
	}
	return NULL;
}

static void start_killer(int m)
{
	pthread_t t;
	mode = m;
	done = 0;
	handler_ran = 0;
	pthread_create(&t, NULL, killer, NULL);
	pthread_detach(t);
}

int main(void)
{
	stack_t ss;
	struct sigaction sa;

	ss.ss_sp = malloc(1 << 16);
	ss.ss_size = 1 << 16;
	ss.ss_flags = 0;
	sigaltstack(&ss, NULL);
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESTART;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGURG, &sa, NULL);
	target = pthread_self();

	start_killer(0);
	while (!handler_ran)
		;
	done = 1;
	printf("CTX A: signal delivered to a spinning thread\n");
	fflush(stdout);
	usleep(200000);

	start_killer(1);
	__asm__ volatile("xor %%eax, %%eax\n1: cmp $0x1234, %%eax\n jne 1b\n" ::: "eax", "cc");
	done = 1;
	printf("CTX B: RAX modification honored\n");
	fflush(stdout);
	usleep(200000);

	start_killer(2);
	for (;;)
		__asm__ volatile("");
	return 0;
}

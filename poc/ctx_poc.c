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

/* Hurd's trampoline calls the handler with, above the return address:
 *   sigreturn_addr, sigreturn_returns_here, return_scp  (then struct sigcontext).
 * __sigreturn() restores from that sigcontext, NOT from the ucontext_t we are
 * given (a copy made by fill_ucontext), so ucontext changes are lost unless
 * they are written back. Compile with -fno-omit-frame-pointer. */
static void handler(int sig, siginfo_t *si, void *uv)
{
	ucontext_t *uc = uv;
	uintptr_t *fp = __builtin_frame_address(0);
	char *scp = (char *)fp[4];
	long *g = uc->uc_mcontext.gregs;
	int off = -1, o;

	handler_ran++;
	for (o = 0; o < 1024; o += 8)
		if (memcmp(scp + o, g, 19 * sizeof(long)) == 0) {
			off = o;
			break;
		}
	if (handler_ran == 1) {
		char buf[160];
		int n = snprintf(buf, sizeof buf, "CTX diag: uc-scp=%ld gregs-block-offset-in-sigcontext=%d\n",
				 (long)((char *)uc - scp), off);
		write(1, buf, n);
	}
	if (mode == 1)
		g[REG_RAX] = 0x1234;
	else if (mode == 2)
		g[REG_RIP] = (greg_t)(uintptr_t)redirect_target;
	if (off >= 0 && mode != 0)
		memcpy(scp + off, g, 19 * sizeof(long)); /* write back for __sigreturn */
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

/* PoC: sigaltstack + SA_SIGINFO + si_addr on SIGSEGV, GNU Hurd.
 *
 * This is the primitive the Go runtime relies on for goroutine stack-guard
 * pages (grow the stack when a guard page is hit) and for turning a
 * nil-pointer dereference into a normal Go panic: it needs (1) a signal
 * handler that runs on an alternate stack even when the faulting thread's
 * own stack is exhausted, and (2) an accurate faulting address in
 * siginfo_t.si_addr.
 */
#define _GNU_SOURCE
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define ALTSTACK_SIZE 65536

static char altstack[ALTSTACK_SIZE];
static void *fault_addr_expected;
static void *fault_addr_got = (void *)(long)-1;
static volatile int got_signal = 0;
static volatile int ran_on_altstack = -1;
static sigjmp_buf jb;

static int on_altstack(void) {
    /* crude check: is a local variable's address inside our altstack range? */
    char local;
    return (&local >= altstack) && (&local < altstack + ALTSTACK_SIZE);
}

static void handler(int sig, siginfo_t *si, void *ucontext) {
    (void)sig;
    (void)ucontext;
    got_signal = 1;
    fault_addr_got = si->si_addr;
    ran_on_altstack = on_altstack();
    siglongjmp(jb, 1);
}

int main(void) {
    stack_t ss;
    ss.ss_sp = altstack;
    ss.ss_size = ALTSTACK_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        perror("sigaltstack");
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, NULL) != 0) {
        perror("sigaction");
        return 1;
    }

    long pagesize = sysconf(_SC_PAGESIZE);
    void *page = mmap(NULL, (size_t)pagesize, PROT_NONE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    fault_addr_expected = page;

    if (sigsetjmp(jb, 1) == 0) {
        volatile char *p = (volatile char *)page;
        *p = 42; /* deliberate fault: write to a PROT_NONE page */
        printf("RESULT: FAIL no fault occurred\n");
        return 1;
    }

    printf("RESULT: got_signal=%d ran_on_altstack=%d si_addr_match=%d expected=%p got=%p\n",
           got_signal, ran_on_altstack,
           fault_addr_got == fault_addr_expected,
           fault_addr_expected, fault_addr_got);
    return (got_signal && ran_on_altstack == 1 && fault_addr_got == fault_addr_expected) ? 0 : 1;
}

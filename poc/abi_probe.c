/* Dump exact sizeof/offsetof values for the GNU Hurd glibc ABI structures
 * needed to hand-write Go runtime defs_hurd*.go (cgo -godefs is not
 * available cross-target, so we probe the real headers with the real
 * compiler instead). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <time.h>
#include <unistd.h>

#define P(name) printf(#name "=%ld\n", (long)(name))
#define SZ(t) printf("sizeof(" #t ")=%zu\n", sizeof(t))
#define OFF(t, f) printf("offsetof(" #t "," #f ")=%zu\n", offsetof(t, f))

int main(void) {
    printf("--- constants ---\n");
    P(PTHREAD_CREATE_DETACHED);
    P(PTHREAD_CREATE_JOINABLE);
    P(ITIMER_REAL);
    P(ITIMER_VIRTUAL);
    P(ITIMER_PROF);
#ifdef HOST_NAME_MAX
    P(HOST_NAME_MAX);
#else
    printf("HOST_NAME_MAX=undefined_on_hurd\n");
#endif
    P(_SC_NPROCESSORS_ONLN);
    P(_SC_PAGESIZE);
    P(RLIMIT_AS);
    P(FPE_INTDIV);
    P(FPE_INTOVF);
    P(FPE_FLTDIV);
    P(FPE_FLTOVF);
    P(FPE_FLTUND);
    P(FPE_FLTRES);
    P(FPE_FLTINV);
    P(FPE_FLTSUB);
    P(BUS_ADRALN);
    P(BUS_ADRERR);
    P(BUS_OBJERR);
    P(SEGV_MAPERR);
    P(SEGV_ACCERR);
    P(__NGREG);
    P(REG_R8); P(REG_R9); P(REG_R10); P(REG_R11); P(REG_R12); P(REG_R13);
    P(REG_R14); P(REG_R15); P(REG_RDI); P(REG_RSI); P(REG_RBP); P(REG_RSP);
    P(REG_RBX); P(REG_RDX); P(REG_RCX); P(REG_RAX); P(REG_RIP); P(REG_CS);
    P(REG_RFL); P(REG_ERR); P(REG_TRAPNO); P(REG_OLDMASK); P(REG_CR2);

    printf("--- sizes ---\n");
    SZ(pthread_t);
    SZ(pthread_attr_t);
    SZ(sem_t);
    SZ(sigset_t);
    SZ(stack_t);
    SZ(siginfo_t);
    SZ(struct sigaction);
    SZ(ucontext_t);
    SZ(mcontext_t);
    SZ(gregset_t);
    SZ(greg_t);
    SZ(struct stat);
    SZ(struct timespec);
    SZ(struct timeval);
    SZ(struct itimerval);
    SZ(off_t);
    SZ(ino_t);
    SZ(dev_t);
    SZ(mode_t);
    SZ(nlink_t);
    SZ(blksize_t);
    SZ(blkcnt_t);

    printf("--- offsets: stack_t ---\n");
    OFF(stack_t, ss_sp);
    OFF(stack_t, ss_flags);
    OFF(stack_t, ss_size);

    printf("--- offsets: siginfo_t ---\n");
    OFF(siginfo_t, si_signo);
    OFF(siginfo_t, si_errno);
    OFF(siginfo_t, si_code);
    OFF(siginfo_t, si_pid);
    OFF(siginfo_t, si_uid);
    OFF(siginfo_t, si_addr);
    OFF(siginfo_t, si_status);
    OFF(siginfo_t, si_band);
    OFF(siginfo_t, si_value);

    printf("--- offsets: struct sigaction ---\n");
    OFF(struct sigaction, sa_mask);
    OFF(struct sigaction, sa_flags);

    printf("--- offsets: ucontext_t ---\n");
    OFF(ucontext_t, uc_flags);
    OFF(ucontext_t, uc_link);
    OFF(ucontext_t, uc_stack);
    OFF(ucontext_t, uc_mcontext);
    OFF(ucontext_t, uc_sigmask);

    printf("--- offsets: mcontext_t ---\n");
    OFF(mcontext_t, gregs);
    OFF(mcontext_t, fpregs);

    printf("--- offsets: struct stat ---\n");
    OFF(struct stat, st_dev);
    OFF(struct stat, st_ino);
    OFF(struct stat, st_mode);
    OFF(struct stat, st_nlink);
    OFF(struct stat, st_uid);
    OFF(struct stat, st_gid);
    OFF(struct stat, st_rdev);
    OFF(struct stat, st_size);
    OFF(struct stat, st_blksize);
    OFF(struct stat, st_blocks);
    OFF(struct stat, st_atim);
    OFF(struct stat, st_mtim);
    OFF(struct stat, st_ctim);

    printf("--- offsets: struct itimerval ---\n");
    OFF(struct itimerval, it_interval);
    OFF(struct itimerval, it_value);

    printf("RESULT: abi_probe_done\n");
    return 0;
}

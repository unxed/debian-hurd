#!/bin/bash
# Runs ON real GNU/Hurd amd64 (from run_poc.py). Produces, from the real glibc
# headers and the real libc, everything the Go syscall port needs:
#   zerrors_hurd_amd64.go  (constants, Errno/Signal, error+signal tables)
#   ztypes_hurd_amd64.go   (struct layouts, from mkztypes_hurd.c)
#   symprobe.txt           (which libc/libpthread exports which function; getdirentries/getentropy/errno smoke test)
# Each file is printed base64-encoded between @@@BEGIN name@@@ / @@@END name@@@
# markers (a serial console must not be trusted with tabs/long lines);
# the workflow decodes them into an artifact. Names are taken from #defines with
# the same filter as syscall/mkerrors.sh.
set -u
cd "$(dirname "$0")"
mkdir -p out
cd out
CC=gcc

emit() { # emit <name> <file>
	echo "@@@BEGIN $1@@@"
	base64 "$2"
	echo "@@@END $1@@@"
}

cat > hdr.h <<'EOF'
#define _GNU_SOURCE
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <dirent.h>
#include <limits.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/icmp6.h>
EOF

echo "== mkhurd: collecting #define names"
$CC -x c hdr.h -E -dM 2>err.E |
	awk '
		$1 != "#define" || $2 ~ /\(/ || $3 == "" {next}

		$2 ~ /^E([ABCD]X|[BIS]P|[SD]I|S|FL)$/ {next}
		$2 ~ /^(SIGEV_|SIGSTKSZ|SIGRT(MIN|MAX))/ {next}
		$2 ~ /^(SCM_SRCRT)$/ {next}
		$2 ~ /^(MAP_FAILED)$/ {next}
		$2 ~ /^CLONE_[A-Z_]+/ {next}
		$2 ~ /^ELF_.*$/ {next}

		$2 !~ /^ETH_/ &&
		$2 !~ /^EPROC_/ &&
		$2 !~ /^EQUIV_/ &&
		$2 !~ /^EXPR_/ &&
		$2 ~ /^E[A-Z0-9_]+$/ ||
		$2 ~ /^B[0-9_]+$/ ||
		$2 ~ /^V[A-Z0-9]+$/ ||
		$2 ~ /^CS[A-Z0-9]/ ||
		$2 ~ /^I(SIG|CANON|CRNL|EXTEN|MAXBEL|STRIP|UTF8)$/ ||
		$2 ~ /^IGN/ ||
		$2 ~ /^IX(ON|ANY|OFF)$/ ||
		$2 ~ /^IN(LCR|PCK)$/ ||
		$2 ~ /(^FLU?SH)|(FLU?SH$)/ ||
		$2 ~ /^C(LOCAL|READ)$/ ||
		$2 == "BRKINT" ||
		$2 == "HUPCL" ||
		$2 == "PENDIN" ||
		$2 == "TOSTOP" ||
		$2 ~ /^PAR/ ||
		$2 ~ /^SIG[^_]/ ||
		$2 ~ /^O[CNPFP][A-Z]+[^_][A-Z]+$/ ||
		$2 ~ /^IN_/ ||
		$2 ~ /^LOCK_(SH|EX|NB|UN)$/ ||
		$2 ~ /^(AF|SOCK|SO|SOL|IPPROTO|IP|IPV6|ICMP6|TCP|EVFILT|NOTE|EV|SHUT|PROT|MAP|PACKET|MSG|SCM|MCL|DT|MADV|PR)_/ ||
		$2 == "ICMPV6_FILTER" ||
		$2 == "SOMAXCONN" ||
		$2 == "NAME_MAX" ||
		$2 == "IFNAMSIZ" ||
		$2 ~ /^(MS|MNT)_/ ||
		$2 ~ /^(O|F|FD|NAME|S|PTRACE|PT)_/ ||
		$2 ~ /^SIOC/ ||
		$2 ~ /^TIOC/ ||
		$2 ~ /^(IFF|IFT)_/ ||
		$2 ~ /^RUSAGE_(SELF|CHILDREN|THREAD)/ ||
		$2 ~ /^RLIMIT_(AS|CORE|CPU|DATA|FSIZE|NOFILE|STACK)|RLIM_INFINITY/ ||
		$2 ~ /^PRIO_(PROCESS|PGRP|USER)/ ||
		$2 ~ /^POLL[A-Z]+$/ ||
		$2 ~ /^AT_[A-Z_]+$/ ||
		$2 !~ "WMESGLEN" &&
		$2 ~ /^W[A-Z0-9]+$/ {print $2}
		{next}
	' | sort -u > names.txt
wc -l < names.txt

# Error / signal name lists (as in mkerrors.sh).
echo '#include <errno.h>' | $CC -x c - -E -dM |
	awk '$1=="#define" && $2 ~ /^E[A-Z0-9_]+$/ { print $2 }' | sort -u > errnames.txt
echo '#include <signal.h>' | $CC -x c - -E -dM |
	awk '$1=="#define" && $2 ~ /^SIG[A-Z0-9]+$/ { print $2 }' |
	grep -v 'SIGSTKSIZE\|SIGSTKSZ\|SIGRT' | sort -u > signames.txt
sort -u errnames.txt signames.txt > errsig.txt
cat errsig.txt names.txt | sort -u > allnames.txt

# One PR() per line so a gcc error line maps back to a name; drop names that
# do not compile as integer constant expressions and retry.
: > dropped.txt
for iter in 1 2 3 4 5 6; do
	{
		echo '#include "hdr.h"'
		echo '#include <stdio.h>'
		echo '#define PR(n) do { long long v_ = (long long)(n); if (v_ < 0) printf("%s -0x%llx\n", #n, (unsigned long long)-v_); else printf("%s 0x%llx\n", #n, (unsigned long long)v_); } while (0)'
		echo 'int main(void) {'
		while read -r n; do echo "PR($n);"; done < allnames.txt
		echo 'return 0; }'
	} > consts.c
	if $CC -w -o consts consts.c 2> consts.err; then
		break
	fi
	sed -n 's/^consts\.c:\([0-9][0-9]*\):[0-9]*: error.*/\1/p' consts.err | sort -un > badlines.txt
	if [ ! -s badlines.txt ]; then
		echo "== mkhurd: consts.c failed outside PR lines:"
		head -30 consts.err
		break
	fi
	# line 5 of consts.c is the first PR line == first line of allnames.txt
	awk 'NR==FNR{bad[$1+0-4]=1; next} FNR in bad {print > "dropped.txt.new"; next} {print}' badlines.txt allnames.txt > allnames.next
	[ -f dropped.txt.new ] && cat dropped.txt.new >> dropped.txt && rm -f dropped.txt.new
	mv allnames.next allnames.txt
done
echo "== mkhurd: dropped (not integer constants): $(tr '\n' ' ' < dropped.txt)"
./consts > vals.txt
wc -l < vals.txt

pad() { # stdin lines "NAME VALUE" -> "\tNAME = VALUE" aligned
	awk '{n[NR]=$1; v[NR]=$2; if (length($1) > m) m = length($1)}
	     END {for (i = 1; i <= NR; i++) printf "\t%-*s = %s\n", m, n[i], v[i]}'
}

# Error / signal tables (strerror/strsignal); Mach errno index = errno & 0xffff.
{
	cat <<'EOF'
#include "hdr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define nelem(x) (sizeof(x)/sizeof((x)[0]))
enum { A = 'A', Z = 'Z', a = 'a', z = 'z' };
static int errors[] = {
EOF
	while read -r n; do grep -qx "$n" dropped.txt || echo "	$n,"; done < errnames.txt
	cat <<'EOF'
};
static int signals[] = {
EOF
	while read -r n; do grep -qx "$n" dropped.txt || echo "	$n,"; done < signames.txt
	cat <<'EOF'
};
static int intcmp(const void *a, const void *b) { return *(int *)a - *(int *)b; }
static int idx(int e) { return ((e >> 16) == 0x4000) ? (e & 0xffff) : e; }
int main(void) {
	int i, e;
	char buf[1024];
	printf("\n// Error table (index: errno & 0xffff for Mach error codes)\n");
	printf("var errors = [...]string{\n");
	qsort(errors, nelem(errors), sizeof errors[0], intcmp);
	for (i = 0; i < nelem(errors); i++) {
		e = errors[i];
		if (i > 0 && idx(errors[i-1]) == idx(e)) continue;
		strcpy(buf, strerror(e));
		if (A <= buf[0] && buf[0] <= Z && a <= buf[1] && buf[1] <= z) buf[0] += a - A;
		printf("\t%d: \"%s\",\n", idx(e), buf);
	}
	printf("}\n\n");
	printf("// Signal table\n");
	printf("var signals = [...]string{\n");
	qsort(signals, nelem(signals), sizeof signals[0], intcmp);
	for (i = 0; i < nelem(signals); i++) {
		e = signals[i];
		if (i > 0 && signals[i-1] == e) continue;
		strcpy(buf, strsignal(e));
		if (A <= buf[0] && buf[0] <= Z && a <= buf[1] && buf[1] <= z) buf[0] += a - A;
		printf("\t%d: \"%s\",\n", e, buf);
	}
	printf("}\n");
	return 0;
}
EOF
} > tables.c
if $CC -w -o tables tables.c 2> tables.err; then
	./tables > tables.go
else
	echo "== mkhurd: tables.c FAILED"; head -30 tables.err
	: > tables.go
fi

# Assemble zerrors_hurd_amd64.go
{
	echo '// Code generated by poc/mkhurd.sh run on real GNU/Hurd amd64; DO NOT EDIT.'
	echo
	echo 'package syscall'
	echo
	echo 'const ('
	awk 'NR==FNR{skip[$1]=1; next} !($1 in skip)' errsig.txt vals.txt | pad
	echo ')'
	echo
	echo '// Errors (Mach error codes: 0x40000000 | n)'
	echo 'const ('
	awk 'NR==FNR{e[$1]=1; next} ($1 in e){print $1, "Errno(" $2 ")"}' errnames.txt vals.txt | pad
	echo ')'
	echo
	echo '// Signals'
	echo 'const ('
	awk 'NR==FNR{e[$1]=1; next} ($1 in e){print $1, "Signal(" $2 ")"}' signames.txt vals.txt | pad
	echo ')'
	cat tables.go
} > zerrors_hurd_amd64.go
wc -l zerrors_hurd_amd64.go

# ztypes
if $CC -w -o mkztypes ../mkztypes_hurd.c 2> ztypes.err; then
	:
else
	echo "== mkhurd: mkztypes_hurd.c FAILED with sockets; retrying with -DNO_SOCK"
	head -40 ztypes.err
	$CC -w -DNO_SOCK -o mkztypes ../mkztypes_hurd.c 2> ztypes.err2 || { echo "== still failing"; head -40 ztypes.err2; }
fi
if [ -x mkztypes ]; then ./mkztypes > ztypes_hurd_amd64.go; else : > ztypes_hurd_amd64.go; fi
wc -l ztypes_hurd_amd64.go

# Symbol / runtime smoke probe.
cat > symprobe.c <<'EOF'
#define _GNU_SOURCE
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#endif
#endif

static const char *syms[] = {
	"access", "accept", "accept4", "adjtime", "bind", "chdir", "chmod", "chown", "chroot", "close",
	"closedir", "connect", "dup", "dup2", "dup3", "execve", "_exit", "exit", "fchdir", "fchmod",
	"fchmodat", "fchown", "fchownat", "fcntl", "fdopendir", "flock", "fork", "fpathconf", "fstat",
	"fstatat", "fsync", "ftruncate", "futimens", "futimes", "getcwd", "getdirentries", "getdents",
	"getegid", "getentropy", "geteuid", "getgid", "getgroups", "gethostname", "getpagesize", "getpeername",
	"getpgid", "getpid", "getppid", "getpriority", "getrandom", "getrlimit", "getrusage", "getsockname",
	"getsockopt", "gettimeofday", "getuid", "ioctl", "kill", "lchown", "link", "linkat", "listen",
	"lseek", "lstat", "mkdir", "mkdirat", "mkfifo", "mknod", "mmap", "munmap", "nanosleep", "open",
	"openat", "opendir", "pipe", "pipe2", "poll", "pread", "pwrite", "read", "readdir", "readdir_r",
	"readlink", "readlinkat", "readv", "recvfrom", "recvmsg", "rename", "renameat", "rmdir", "select",
	"sendmsg", "sendto", "setgid", "setgroups", "setitimer", "setpgid", "setpriority", "setrlimit",
	"setsid", "setsockopt", "setuid", "shutdown", "sigaction", "socket", "socketpair", "stat",
	"symlink", "sync", "sysconf", "truncate", "umask", "uname", "unlink", "unlinkat", "utimensat",
	"utimes", "wait4", "waitpid", "write", "writev", "__errno_location", "sched_yield",
	"clock_gettime", "lstat64", "stat64", "fstat64", "__xstat", "__fxstat", "__lxstat", "issetugid",
	"pthread_create", "pthread_kill", "pthread_self", "sem_wait", "posix_spawn", "getdirentries64",
	"login_tty", "openpty", "forkpty", "posix_openpt", "ptsname", "grantpt", "unlockpt", "ptsname_r",
	"getaddrinfo", "getnameinfo", "res_init", "__res_init", 0
};

int main(void)
{
	void *lc = dlopen("libc.so.0.3", RTLD_LAZY);
	void *lp = dlopen("libpthread.so.0.3", RTLD_LAZY);
	int i;
	printf("dlopen libc=%p libpthread=%p\n", lc, lp);
	for (i = 0; syms[i]; i++) {
		void *a = lc ? dlsym(lc, syms[i]) : 0;
		void *b = lp ? dlsym(lp, syms[i]) : 0;
		void *g = dlsym(RTLD_DEFAULT, syms[i]);
		printf("SYM %-18s libc=%d pthread=%d default=%d\n", syms[i], a != 0, b != 0, g != 0);
	}

	/* errno after a failing open: must be a Mach code */
	errno = 0;
	int fd = open("/nonexistent-dir/x", O_RDONLY);
	printf("open(nonexistent)=%d errno=0x%x ENOENT=0x%x\n", fd, errno, ENOENT);

	/* getdirentries smoke test: layout and record walking */
	fd = open("/", O_RDONLY | O_DIRECTORY);
	if (fd >= 0) {
		char buf[4096];
		off_t base = 0;
		ssize_t n = getdirentries(fd, buf, sizeof buf, &base);
		printf("getdirentries n=%zd base=%ld\n", n, (long)base);
		ssize_t off = 0;
		int k = 0;
		while (off < n && k < 6) {
			struct dirent *d = (struct dirent *)(buf + off);
			printf("  DIRENT off=%zd fileno=%lu reclen=%u type=%u namlen=%u name=%s\n",
				off, (unsigned long)d->d_fileno, (unsigned)d->d_reclen, (unsigned)d->d_type,
				(unsigned)d->d_namlen, d->d_name);
			if (d->d_reclen == 0)
				break;
			off += d->d_reclen;
			k++;
		}
		close(fd);
	} else
		printf("open(/) failed errno=0x%x\n", errno);

	/* entropy */
	unsigned char rb[16];
	printf("getentropy=%d getrandom=%zd\n", getentropy(rb, sizeof rb), getrandom(rb, sizeof rb, 0));

	/* stat smoke */
	struct stat st;
	int r = stat("/", &st);
	printf("stat(/)=%d mode=0%o size=%ld blksize=%ld nlink=%lu\n", r, (unsigned)st.st_mode,
		(long)st.st_size, (long)st.st_blksize, (unsigned long)st.st_nlink);
	printf("RESULT: symprobe_done\n");
	return 0;
}
EOF
$CC -w -o symprobe symprobe.c -ldl 2> symprobe.err || $CC -w -o symprobe symprobe.c 2>> symprobe.err
if [ -x symprobe ]; then ./symprobe > symprobe.txt 2>&1; else cp symprobe.err symprobe.txt; echo "symprobe compile FAILED"; fi
cat symprobe.txt

emit zerrors_hurd_amd64.go zerrors_hurd_amd64.go
emit ztypes_hurd_amd64.go ztypes_hurd_amd64.go
emit symprobe.txt symprobe.txt
echo "RESULT: mkhurd_done"

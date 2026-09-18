/* PTY facts on real GNU/Hurd for the f4 terminal backend: which allocation API works,
 * what the slave is called, whether the master is pollable, controlling tty, window size. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define SHOW(fmt, ...) do { printf("PTY " fmt "\n", ##__VA_ARGS__); fflush(stdout); } while (0)

static void drain(int m, const char *what, int ms)
{
	char b[512];
	struct pollfd p = {m, POLLIN, 0};
	int r, total = 0;
	while ((r = poll(&p, 1, ms)) > 0 && (p.revents & POLLIN)) {
		ssize_t k = read(m, b, sizeof b - 1);
		if (k <= 0)
			break;
		b[k] = 0;
		for (ssize_t i = 0; i < k; i++)
			if (b[i] == '\r')
				b[i] = '~';
		SHOW("%s read %zd: [%s]", what, k, b);
		total += k;
	}
	SHOW("%s poll rc=%d revents=0x%x total=%d", what, r, p.revents, total);
}

int main(void)
{
	int m, s;
	char *name;
	struct winsize ws = {24, 80, 0, 0}, w2 = {0, 0, 0, 0};
	struct termios t;
	int pg = -1;

	errno = 0;
	m = posix_openpt(O_RDWR | O_NOCTTY);
	SHOW("posix_openpt=%d errno=0x%x", m, errno);
	if (m < 0)
		return 1;
	SHOW("grantpt=%d unlockpt=%d", grantpt(m), unlockpt(m));
	name = ptsname(m);
	SHOW("ptsname=%s", name ? name : "(null)");
	if (!name)
		return 2;
	s = open(name, O_RDWR | O_NOCTTY);
	SHOW("open slave=%d errno=0x%x", s, errno);
	if (s < 0)
		return 3;
	SHOW("tcgetattr(slave)=%d ICANON=%d ECHO=%d", tcgetattr(s, &t), !!(t.c_lflag & ICANON), !!(t.c_lflag & ECHO));
	SHOW("TIOCSWINSZ(master)=%d", ioctl(m, TIOCSWINSZ, &ws));
	SHOW("TIOCGWINSZ(slave)=%d %dx%d", ioctl(s, TIOCGWINSZ, &w2), w2.ws_col, w2.ws_row);
	SHOW("TIOCGPGRP(master)=%d pgrp=%d errno=0x%x", ioctl(m, TIOCGPGRP, &pg), pg, errno);
	SHOW("O_NONBLOCK on master: fcntl=%d", fcntl(m, F_SETFL, fcntl(m, F_GETFL) | O_NONBLOCK));

	write(s, "from-slave\n", 11);
	drain(m, "slave->master", 1500);

	write(m, "from-master\n", 12);
	{
		char b[64];
		ssize_t k = read(s, b, sizeof b - 1);
		if (k > 0) { b[k] = 0; SHOW("master->slave read %zd: [%s]", k, b); } else SHOW("master->slave read=%zd errno=0x%x", k, errno);
	}

	/* child on the slave: new session + controlling tty, like Go's SysProcAttr{Setsid, Setctty} */
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		dup2(s, 0); dup2(s, 1); dup2(s, 2);
		int r = ioctl(0, TIOCSCTTY, 0);
		fprintf(stderr, "child TIOCSCTTY=%d errno=0x%x\n", r, errno);
		execl("/bin/sh", "sh", "-c", "echo child-tty=$(tty); stty size; kill -0 $$ && echo child-done", (char *)0);
		_exit(127);
	}
	drain(m, "child", 3000);
	int st = 0;
	waitpid(pid, &st, 0);
	SHOW("child status=0x%x", st);
	SHOW("RESULT: pty_poc_done");
	return 0;
}

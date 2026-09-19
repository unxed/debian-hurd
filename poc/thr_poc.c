/* How many threads can a Hurd process create? Which errno ends it? Does the stack size matter? */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static sem_t gate;
static void *body(void *a) { sem_wait(&gate); return 0; }

static int try(const char *what, size_t stack, int max)
{
	pthread_attr_t at;
	pthread_t t;
	int i, rc = 0;
	sem_init(&gate, 0, 0);
	for (i = 0; i < max; i++) {
		pthread_attr_init(&at);
		pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
		if (stack)
			pthread_attr_setstacksize(&at, stack);
		rc = pthread_create(&t, &at, body, NULL);
		pthread_attr_destroy(&at);
		if (rc)
			break;
	}
	printf("THR %s: created=%d rc=0x%x (%s)\n", what, i, rc, rc ? strerror(rc) : "ok");
	fflush(stdout);
	for (int k = 0; k < i; k++)
		sem_post(&gate);
	usleep(300000);
	return i;
}

int main(void)
{
	size_t dflt = 0;
	pthread_attr_t at;
	pthread_attr_init(&at);
	pthread_attr_getstacksize(&at, &dflt);
	printf("THR default stacksize=%zu\n", dflt);
	try("default-stack", 0, 400);
	try("256K-stack", 256 * 1024, 400);
	try("64K-stack", 64 * 1024, 400);
	{	/* address space / commit: how much anonymous memory can we take? */
		size_t total = 0, chunk = 64UL << 20;
		void *p;
		while ((p = mmap(0, chunk, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0)) != MAP_FAILED && total < (16UL << 30))
			total += chunk;
		printf("THR mmap anon (untouched) before failure: %zu MB errno=0x%x\n", total >> 20, errno);
	}
	printf("RESULT: thr_poc_done\n");
	return 0;
}

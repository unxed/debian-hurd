CC=gcc
CFLAGS=-O2 -Wall -pthread

all: futex_poc sig_poc

futex_poc: futex_poc.c
	$(CC) $(CFLAGS) -o $@ $<

sig_poc: sig_poc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f futex_poc sig_poc

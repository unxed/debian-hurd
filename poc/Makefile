CC=gcc
CFLAGS=-O2 -Wall -pthread

all: futex_poc sig_poc io_poc segv_poc abi_probe ctx_poc pty_poc thr_poc

futex_poc: futex_poc.c
	$(CC) $(CFLAGS) -o $@ $<

sig_poc: sig_poc.c
	$(CC) $(CFLAGS) -o $@ $<

io_poc: io_poc.c
	$(CC) $(CFLAGS) -o $@ $<

segv_poc: segv_poc.c
	$(CC) $(CFLAGS) -o $@ $<

abi_probe: abi_probe.c
	$(CC) $(CFLAGS) -o $@ $<

thr_poc: thr_poc.c
	$(CC) $(CFLAGS) -o $@ $<

pty_poc: pty_poc.c
	$(CC) $(CFLAGS) -o $@ $<

ctx_poc: ctx_poc.c
	$(CC) $(CFLAGS) -O0 -fno-omit-frame-pointer -o $@ $<

clean:
	rm -f futex_poc sig_poc io_poc segv_poc abi_probe ctx_poc

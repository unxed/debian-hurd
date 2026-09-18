#!/usr/bin/env python3
"""Boot the Hurd image under QEMU over a serial console and run the futex/signal PoCs."""
import glob
import os
import sys
import time

import pexpect

IMG = sys.argv[1]

use_kvm = os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK) and os.environ.get("USE_KVM", "1") == "1"
accel = "kvm -cpu host" if use_kvm else "tcg,thread=single -cpu max"
cmd = (
    f"qemu-system-x86_64 -m 2048 -smp 1 -no-reboot -accel {accel} "
    f"-drive file={IMG},format=raw,if=ide "
    f"-display none -serial stdio -monitor none"
)
print(f"KVM: {use_kvm}", flush=True)

print(f"+ {cmd}", flush=True)
child = pexpect.spawn(cmd, timeout=600, encoding="utf-8", codec_errors="replace")
child.logfile = sys.stdout

PROMPT = r"[#\$] $"
exit_code = 1

try:
    child.expect(["login:", "Login:"], timeout=480)
    child.sendline("root")
    idx = child.expect(["Password:", "assword:", PROMPT], timeout=30)
    if idx in (0, 1):
        child.sendline("")
        child.expect(PROMPT, timeout=30)

    child.sendline("cd /root/poc && make -k 2>&1 ; echo MAKE_RC_$?")
    child.expect(r"MAKE_RC_\d+", timeout=120)

    child.sendline("./futex_poc ; echo FUTEX_RC_$?")
    child.expect(r"FUTEX_RC_\d+", timeout=120)

    child.sendline("./sig_poc ; echo SIG_RC_$?")
    child.expect(r"SIG_RC_\d+", timeout=120)

    child.sendline("./io_poc ; echo IO_RC_$?")
    child.expect(r"IO_RC_\d+", timeout=120)

    child.sendline("./segv_poc ; echo SEGV_RC_$?")
    child.expect(r"SEGV_RC_\d+", timeout=120)

    child.sendline("./abi_probe ; echo ABI_RC_$?")
    child.expect(r"ABI_RC_\d+", timeout=120)

    child.sendline("./ctx_poc ; echo CTX_RC_$?")
    child.expect(r"CTX_RC_\d+", timeout=60)

    child.sendline("ls -l /dev/ptmx /dev/pts /dev/ptyp0 /dev/ttyp0 /dev/tty 2>&1 | head -12; showtrans /dev/ptyp0 /dev/ttyp0 /dev/ptmx 2>&1 | head -5; echo PTYLS_DONE")
    child.expect("PTYLS_DONE", timeout=30)
    child.sendline("timeout -s KILL 60 ./pty_poc 2>&1 ; echo PTY_RC_$?")
    child.expect(r"PTY_RC_\d+", timeout=90)

    child.sendline("./hurdhello.bin ; echo HURDHELLO_RC_$?")
    child.expect(r"HURDHELLO_RC_\d+", timeout=60)

    # Go programs cross-compiled by unxed/go (poc/gotests/*.bin).
    for path in sorted(glob.glob("poc/gotests/*.bin")):
        name = os.path.basename(path)[:-4]
        # `timeout` inside the guest: a hung test must not take the whole run down
        # (Ctrl-C on our side would hit QEMU itself).
        child.sendline(f"timeout -s KILL 60 /root/gotests/{name}.bin 2>&1 ; echo GOTEST_{name}_RC_$?")
        try:
            child.expect(rf"GOTEST_{name}_RC_\d+", timeout=120)
        except pexpect.TIMEOUT:
            print(f"\n*** TIMEOUT in {name} (guest timeout did not fire) ***", flush=True)
            break

    # f4 (unxed/f4 cross-built by unxed/go hurd-f4-build): does the TUI start, run a command, quit?
    if os.path.exists("poc/f4/f4.gz"):
        child.sendline("mkdir -p /root/f4home /root/f4work && gunzip -c /root/f4/f4.gz > /root/f4/f4 && chmod +x /root/f4/f4 ; echo F4_UNPACK_$?")
        child.expect(r"F4_UNPACK_\d+", timeout=400)
        child.sendline("killall f4 2>/dev/null ; rm -rf /tmp/f4-sessions-0 /root/f4home/.config ; export TERM=xterm-256color HOME=/root/f4home ; cd /root/f4work ; /root/f4/f4 --version ; echo F4_VERSION_$?")
        child.expect(r"F4_VERSION_\d+", timeout=90)
        child.sendline("stty rows 24 cols 80 ; cd / ; timeout --foreground -s KILL 240 /root/f4/f4 ; echo F4_EXIT_$? ; cd /root/poc")
        child.expect(pexpect.TIMEOUT, timeout=40)            # let it start and draw the panels
        child.send("\x1b[B\x1b[B")                          # Down, Down
        child.expect(pexpect.TIMEOUT, timeout=5)
        child.send("echo f4-$((20+22))-ok\r")                 # command line -> terminal view -> pty -> sh
        child.expect(pexpect.TIMEOUT, timeout=20)
        child.send("\x1b[21~")                               # F10 -> "Leave f4?" dialog
        child.expect(pexpect.TIMEOUT, timeout=5)
        child.send("\r")                                     # Leave
        try:
            child.expect(r"F4_EXIT_\d+", timeout=60)
        except pexpect.TIMEOUT:
            print("\n*** f4 did not quit: trying again ***", flush=True)
            child.send("\x1b[21~")
            child.expect(pexpect.TIMEOUT, timeout=3)
            child.send("\r")
            child.expect(r"F4_EXIT_\d+", timeout=250)         # the guest-side timeout bounds this
        child.sendline("killall f4 2>/dev/null ; ls /tmp/f4-sessions-0 2>&1 | head -3 ; ls /root/f4home/.config/f4/crashes 2>&1 | head ; tail -25 /root/f4home/.config/f4/logs/debug.log ; echo F4_POST_DONE")
        child.expect("F4_POST_DONE", timeout=60)

    # Async preemption on/off comparison for a program that failed with it on.
    for name in ("t_fmt", "t_exec"):
        if os.path.exists(f"poc/gotests/{name}.bin"):
            child.sendline(f"GODEBUG=asyncpreemptoff=1 timeout -s KILL 60 /root/gotests/{name}.bin 2>&1 ; echo GOTEST_{name}_nopreempt_RC_$?")
            child.expect(rf"GOTEST_{name}_nopreempt_RC_\d+", timeout=120)

    # Flakiness statistics for async preemption (see poc/loop_tests.sh).
    if os.environ.get("RUN_LOOPS") == "1":
        child.sendline("sh /root/poc/loop_tests.sh 10")
        child.expect("LOOPS_DONE", timeout=1500)

    # Optional: generate zerrors/ztypes/symbol report from the real headers+libc.
    if os.environ.get("RUN_MKHURD") == "1":
        child.sendline("bash mkhurd.sh ; echo MKHURD_RC_$?")
        child.expect(r"MKHURD_RC_\d+", timeout=1200)

    child.sendline("echo ALL_DONE_MARKER")
    child.expect("ALL_DONE_MARKER", timeout=20)
    time.sleep(1)
    exit_code = 0
except pexpect.TIMEOUT:
    print("\n*** TIMEOUT waiting for expected output ***", flush=True)
except pexpect.EOF:
    print("\n*** EOF (qemu exited unexpectedly) ***", flush=True)
finally:
    try:
        child.sendline("poweroff -f 2>/dev/null || halt -f 2>/dev/null || true")
        time.sleep(3)
    except Exception:
        pass
    try:
        child.close(force=True)
    except Exception:
        pass

sys.exit(exit_code)

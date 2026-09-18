#!/usr/bin/env python3
"""Boot the Hurd image under QEMU over a serial console and run the futex/signal PoCs."""
import glob
import os
import sys
import time

import pexpect

IMG = sys.argv[1]

cmd = (
    f"qemu-system-x86_64 -m 2048 -smp 1 -no-reboot -accel tcg,thread=single "
    f"-cpu max -drive file={IMG},format=raw,if=ide "
    f"-display none -serial stdio -monitor none"
)

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

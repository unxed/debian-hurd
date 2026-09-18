#!/usr/bin/env python3
"""Boot the Hurd image under QEMU over a serial console and run the futex/signal PoCs."""
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

    child.sendline("LD_BIND_NOW=1 ./hurdhello.bin ; echo HURDHELLO_RC_$?")
    child.expect(r"HURDHELLO_RC_\d+", timeout=60)

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

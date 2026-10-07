#!/usr/bin/env python3
"""Drive a real interactive ssh login against soupOS's vault.

ssh insists on a tty for its password prompt, so this uses pexpect. Usage:
    ssh-login.py <port> good    # log in, run whoami and larder, expect answers
    ssh-login.py <port> bad     # wrong password, expect Permission denied
Exit status 0 on the expected outcome, 1 otherwise.
"""
import sys
import pexpect

port, mode = sys.argv[1], sys.argv[2]
opts = ("-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "
        "-o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1")
c = pexpect.spawn(f"ssh -p {port} {opts} headchef@127.0.0.1", encoding="utf-8", timeout=25)
try:
    c.expect("password:")
    if mode == "good":
        c.sendline("rosemary")
        c.expect("share the local keyboard")
        c.sendline("whoami")
        c.expect(r"headchef\s+\(uid 0\)")
        c.sendline("larder")
        c.expect("clusters used")
    else:
        c.sendline("wrongsauce")
        c.expect("Permission denied")
except (pexpect.EOF, pexpect.TIMEOUT) as e:
    print(f"ssh-login: {type(e).__name__} waiting in mode {mode}; got: {c.before!r}", file=sys.stderr)
    sys.exit(1)
finally:
    c.close(force=True)

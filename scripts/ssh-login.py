#!/usr/bin/env python3
"""Drive the real ssh client against soupOS's vault with pexpect, since ssh
insists on a tty for its password prompt.

    ssh-login.py <port> good          log in, run whoami and larder, expect answers
    ssh-login.py <port> bad           wrong password, expect Permission denied
    ssh-login.py <port> exec          `ssh host whoami` with the password: one command, then exit
    ssh-login.py <port> key <file>    log in with an ed25519 key, no password offered
    ssh-login.py <port> busy          hold a session open, then a second client must be told "busy"

Exit status 0 on the expected outcome, 1 otherwise.
"""
import subprocess, sys
import pexpect

port, mode = sys.argv[1], sys.argv[2]
common = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o NumberOfPasswordPrompts=1"
nokey = common + " -o PubkeyAuthentication=no"

def login(extra="", cmd=""):
    c = pexpect.spawn(f"ssh -p {port} {extra} headchef@127.0.0.1 {cmd}", encoding="utf-8", timeout=25)
    return c

try:
    if mode == "good":
        c = login(nokey)
        c.expect("password:"); c.sendline("rosemary")
        c.expect("share the local keyboard")
        c.sendline("whoami"); c.expect(r"headchef\s+\(uid 0\)")
        c.sendline("larder"); c.expect("clusters used")
        c.close(force=True)
    elif mode == "bad":
        c = login(nokey)
        c.expect("password:"); c.sendline("wrongsauce")
        c.expect("Permission denied")
        c.close(force=True)
    elif mode == "exec":
        c = login(nokey, "whoami")
        c.expect("password:"); c.sendline("rosemary")
        c.expect(r"headchef\s+\(uid 0\)")
        c.expect(pexpect.EOF)            # the channel closed when the prompt came back
        c.close()
        if c.exitstatus != 0:
            print(f"ssh-login: exec exit status {c.exitstatus}", file=sys.stderr); sys.exit(1)
    elif mode == "key":
        key = sys.argv[3]
        c = login(common + f" -i {key} -o PasswordAuthentication=no -o IdentitiesOnly=yes", "whoami")
        c.expect(r"headchef\s+\(uid 0\)")
        c.expect(pexpect.EOF); c.close()
    elif mode == "busy":
        c = login(nokey)
        c.expect("password:"); c.sendline("rosemary")
        c.expect("share the local keyboard")
        second = subprocess.run(["ssh", "-p", port] + nokey.split() + ["-o", "ConnectTimeout=10",
                                 "headchef@127.0.0.1"], capture_output=True, text=True, timeout=30)
        c.close(force=True)
        if "vault is busy" not in second.stderr:
            print(f"ssh-login: second client saw: {second.stderr!r}", file=sys.stderr); sys.exit(1)
    else:
        sys.exit(2)
except (pexpect.EOF, pexpect.TIMEOUT) as e:
    print(f"ssh-login: {type(e).__name__} in mode {mode}", file=sys.stderr)
    sys.exit(1)

#!/usr/bin/env python3
"""Drive the real ssh client against soupOS's vault with pexpect, since ssh
insists on a tty for its password prompt.

    ssh-login.py <port> good          log in, run whoami and larder, expect answers
    ssh-login.py <port> bad           wrong password, expect Permission denied
    ssh-login.py <port> exec          `ssh host whoami` with the password: one command, then exit
    ssh-login.py <port> key <file>    log in with an ed25519 key, no password offered
    ssh-login.py <port> busy          hold a session open, then a second client must be told "busy"
    ssh-login.py <port> execfail      `ssh host nonsense` must exit 127, as sh would
    ssh-login.py <port> isolate       a session's own cwd and output: hash zebracake, a bowl,
                                      cd into it, pwd, then clockout must end the session
    ssh-login.py <port> ctrlc         cook hog.elf, Ctrl-C over the wire kills it, shell carries on
    ssh-login.py <port> hangup        cook hog.elf &, then drop the connection with it running
    ssh-login.py <port> rekey         the CLIENT rekeys every 2 KB (RekeyLimit): menu twice, then
                                      whoami must still answer through the new keys

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
        c.expect("a shell of your own")
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
        c.expect("a shell of your own")
        second = subprocess.run(["ssh", "-p", port] + nokey.split() + ["-o", "ConnectTimeout=10",
                                 "headchef@127.0.0.1"], capture_output=True, text=True, timeout=30)
        c.close(force=True)
        if "vault is busy" not in second.stderr:
            print(f"ssh-login: second client saw: {second.stderr!r}", file=sys.stderr); sys.exit(1)
    elif mode == "execfail":
        c = login(nokey, "nonsensecmd")
        c.expect("password:"); c.sendline("rosemary")
        c.expect("No soup for you"); c.expect(pexpect.EOF); c.close()
        if c.exitstatus != 127:
            print(f"ssh-login: exec status {c.exitstatus}, wanted 127", file=sys.stderr); sys.exit(1)
    elif mode == "isolate":
        c = login(nokey)
        c.expect("password:"); c.sendline("rosemary")
        c.expect("a shell of your own")
        c.sendline("hash zebracake"); c.expect("zebracake")
        c.sendline("mkbowl SSHBOWL"); c.sendline("cd SSHBOWL")
        c.sendline("pwd"); c.expect("/SSHBOWL")
        c.sendline("clockout"); c.expect("clocks out"); c.expect(pexpect.EOF)
    elif mode == "ctrlc":
        import time
        c = login(nokey)
        c.expect("password:"); c.sendline("rosemary")
        c.expect("a shell of your own")
        c.sendline("cook hog.elf"); c.expect("running, never yields")
        time.sleep(1); c.sendcontrol("c")
        c.expect(r"hog\.elf killed")
        c.sendline("whoami"); c.expect(r"headchef\s+\(uid 0\)")
        c.close(force=True)
    elif mode == "hangup":
        import time
        c = login(nokey)
        c.expect("password:"); c.sendline("rosemary")
        c.expect("a shell of your own")
        c.sendline("cook hog.elf &"); c.expect("running, never yields")
        c.close(force=True)
        time.sleep(3)                    # the vault notices, hangs the session up
    elif mode == "rekey":
        c = login(nokey + " -o RekeyLimit=2K")
        c.expect("password:"); c.sendline("rosemary")
        c.expect("a shell of your own")
        for _ in range(3):
            c.sendline("roster"); c.expect("headchef")
        c.sendline("whoami"); c.expect(r"headchef\s+\(uid 0\)")
        c.close(force=True)
    else:
        sys.exit(2)
except (pexpect.EOF, pexpect.TIMEOUT) as e:
    print(f"ssh-login: {type(e).__name__} in mode {mode}", file=sys.stderr)
    sys.exit(1)

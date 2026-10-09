#!/usr/bin/env bash
# Does guessing a password over SSH cost time, and then stop working?
#
# Measured before v0.50.0: 124 wrong guesses a minute from one address. Now a
# wrong password pauses its connection (1 s, 2 s, 4 s ...), and ten failures
# inside five minutes shut the address out. This boot of its own (it locks the
# host out, which would break any test after it) checks both, with the real
# client: three wrong guesses on one connection take at least 7 s, and after
# ten the address is refused at the door, even with the right password.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18150
TESTDISK=$(mktemp /tmp/soupos-guessdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-guess.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-guess.XXXXXX.sock)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT
cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "vault 22" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }

python3 - "$PORT" <<'PY'
import pexpect, sys, time
port = sys.argv[1]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10"
fail = 0
def connection(guesses, right=False):
    """One connection: up to `guesses` wrong passwords (then the right one if
    asked). Returns (wrong guesses made, seconds, how it ended)."""
    c = pexpect.spawn(f"ssh -p {port} {o} -o NumberOfPasswordPrompts={guesses + (1 if right else 0)} headchef@127.0.0.1 whoami",
                      encoding="utf-8", timeout=40)
    made, t0 = 0, time.time()
    while True:
        # "Permission denied, please try again" comes between prompts; only
        # "Permission denied (publickey,password)" ends the connection.
        i = c.expect(["password:", r"Permission denied \(", "too many failed logins", r"headchef\s+\(uid 0\)", pexpect.EOF, pexpect.TIMEOUT])
        if i == 0:
            if made < guesses: c.sendline("wrong%d" % made); made += 1
            else: c.sendline("rosemary")
        else:
            c.close(force=True)
            return made, time.time() - t0, ["denied", "denied", "locked", "in", "eof", "timeout"][i]
made, secs, how = connection(3)
if made == 3 and secs >= 7:
    print(f"  ok    three wrong guesses on one connection took {secs:.1f} s (pauses 1+2+4)")
else:
    print(f"  FAIL  three wrong guesses took {secs:.1f} s ({made} made, {how})"); fail = 1
total = made
while total < 10:
    m, _, how = connection(min(3, 10 - total))
    if how == "locked": break
    total += m
made, secs, how = connection(0, right=True)
if how == "locked":
    print(f"  ok    after {total} failures the address is refused at the door, even with the right password")
else:
    print(f"  FAIL  after {total} failures a login with the right password ended '{how}'"); fail = 1
sys.exit(fail)
PY
rc=$?
grep -q "too many failed logins" "$LOG" && echo "  ok    the kernel logged the refusal" \
    || { echo "  FAIL  no refusal in the kernel log; its ssh lines end:"; grep "\[ssh\]" "$LOG" | tail -4 | sed 's/^/        /'; rc=1; }
exit $rc

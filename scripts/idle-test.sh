#!/usr/bin/env bash
# idle-test.sh - idle SSH sessions end (v0.53.3).
#
# One boot. With the default (never), a session left alone for 7 s survives.
# After `vault idle 5`, a silent session is ended within 5-8 s with a
# DISCONNECT the client prints; a session that keeps typing outlives the
# limit; the ended session's slot and shell are gone (`who` on the console);
# the console itself never times out.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18180
TESTDISK=$(mktemp /tmp/soupos-idledisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-idle.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-idle.XXXXXX.sock)
PID=""
fail=0
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$TESTDISK"
            if [ "$fail" != 0 ]; then echo "  serial log kept at $LOG"; else rm -f "$LOG"; fi; }
trap cleanup EXIT
cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "vault 22" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; exit 1; }

python3 - "$PORT" "$MON" <<'PY' || fail=1
import pexpect, subprocess, sys, time
port, mon = sys.argv[1], sys.argv[2]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10"
fail = 0
def say(ok, text):
    global fail
    print(("  ok    " if ok else "  FAIL  ") + text)
    if not ok: fail = 1
def login():
    c = pexpect.spawn(f"ssh -tt -p {port} {o} headchef@127.0.0.1", encoding="utf-8", timeout=30)
    c.expect("password:"); c.sendline("rosemary"); c.expect("Welcome to the kitchen"); c.expect("> ")
    return c
def keys(*k):
    subprocess.run(["python3", "scripts/qemu_keys.py", mon, *k], check=True, capture_output=True)

# The default: never.
c = login(); time.sleep(7); c.sendline("whoami")
say(c.expect([r"headchef\s+\(uid 0\)", pexpect.EOF, pexpect.TIMEOUT], timeout=10) == 0,
    "by default a session idle for 7 s is not ended")
c.sendline("clockout"); c.close(force=True)

keys("vault idle 5", "WAIT:1")
# A silent session ends, and says why.
c = login(); t0 = time.time()
i = c.expect([r"idle for 5 seconds: the vault ended this session", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
secs = time.time() - t0
# t0 is taken once the prompt has arrived, a little after the server starts
# the session's clock, so the measured time can be just under 5.
say(i == 0 and 4.5 <= secs <= 8, f"a silent session is ended after {secs:.1f} s, and the client is told why")
c.close(force=True)
# A typing one does not.
c = login(); t0 = time.time()
while time.time() - t0 < 12:
    c.sendline("whoami"); c.expect(r"headchef\s+\(uid 0\)"); time.sleep(2)
c.sendline("whoami")
say(c.expect([r"headchef\s+\(uid 0\)", pexpect.EOF, pexpect.TIMEOUT], timeout=10) == 0,
    "a session that keeps typing outlives the limit (12 s)")
c.sendline("clockout"); c.close(force=True)
time.sleep(1)
keys("brigade", "WAIT:2")
sys.exit(fail)
PY
grep -q "\[ssh\] headchef idle for 5 s: session ended" "$LOG" \
    && echo "  ok    the ending is logged" || { echo "  FAIL  no idle log line"; fail=1; }
# The console, idle through all of it, still answers, and lists no session.
whoat=$(grep -n "COOK  *WHERE  *UP  *IDLE" "$LOG" | tail -1 | cut -d: -f1)
if [ -n "$whoat" ] && awk -v a="$whoat" 'NR>a' "$LOG" | grep "headchef  *console" >/dev/null \
   && ! awk -v a="$whoat" 'NR>a' "$LOG" | grep "10\.0\.2\.2" >/dev/null; then
    echo "  ok    the console never timed out, and no ended session is left in who"
else
    echo "  FAIL  who after the sessions ended"; fail=1
fi
exit $fail

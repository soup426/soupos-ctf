#!/usr/bin/env bash
# Can the shell be driven over the network?
#
# Opens the pass, connects from the host through QEMU's hostfwd, types two
# commands, and checks their output came back over the socket. The session has
# to be HELD OPEN between commands: piping a string into nc gives it EOF at
# once and it hangs up before the server's one-second accept poll gets to it,
# which looks like a server that never answers.
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=18097
LOG=$(mktemp /tmp/soupos-pass.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-pass.XXXXXX.sock)
TESTDISK=$(mktemp /tmp/soupos-passdisk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:23" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5

python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "plumbing dhcp" "WAIT:6" "pass 23" "WAIT:2" "clockout" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }

# Since v0.55.1 the pass opens only to the login prompt, so the caller logs
# in through it like anyone at the keyboard.
session=$( (sleep 1; printf 'headchef\n'; sleep 1; printf 'rosemary\n'; sleep 3; printf 'whoami\n'; sleep 4; printf 'larder\n'; sleep 4) \
           | timeout 35 nc 127.0.0.1 "$PORT" 2>/dev/null | tr -d '\r')

fail=0
echo "$session" | grep "You share the local keyboard" >/dev/null \
    && echo "  ok    the pass greeted the session" \
    || { echo "  FAIL  no greeting came back"; fail=1; }
echo "$session" | grep "headchef  (uid 0)" >/dev/null \
    && echo "  ok    a command typed over the network ran and answered" \
    || { echo "  FAIL  whoami's output did not come back"; fail=1; }
echo "$session" | grep "clusters used" >/dev/null \
    && echo "  ok    a second command in the same session also answered" \
    || { echo "  FAIL  larder's output did not come back"; fail=1; }
grep -qE "^\[pass\] session open" "$LOG" \
    && echo "  ok    the kernel logged the session" \
    || { echo "  FAIL  no session in the kernel log"; fail=1; }

# ── the pass gets the vault's rules (v0.55.1) ──
# Measured on v0.55.0: with the headchef logged in at the console, a
# stranger on the pass typed `whoami` and was the headchef, no password;
# ten wrong passwords took 8 s, the client's own pace; and refusals were
# recorded as "console". A second boot, so the first one's session is gone.
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
cp disk.img "$TESTDISK"; : > "$LOG"; rm -f "$MON"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:23" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "pass 23" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
python3 - "$PORT" "$MON" <<'PY' || fail=1
import socket, subprocess, sys, time
port, mon = int(sys.argv[1]), sys.argv[2]
fail = 0
def say(ok, text):
    global fail
    print(("  ok    " if ok else "  FAIL  ") + text); fail |= (not ok)
def call():
    s = socket.create_connection(("127.0.0.1", port), timeout=10); s.settimeout(0.5); return s
def read(s, t):
    out, end = b"", time.time() + t
    while time.time() < end:
        try:
            b = s.recv(65536)
            if not b: break
            out += b
        except socket.timeout: pass
    return out.decode("latin1")
# 1. The headchef is logged in at the console: the caller is turned away.
s = call(); time.sleep(0.5); s.sendall(b"whoami\r"); got = read(s, 3); s.close()
say("console is in use" in got and "headchef  (uid 0)" not in got,
    "with the headchef logged in at the console, a caller is turned away, not handed the shell")
subprocess.run(["python3", "scripts/qemu_keys.py", mon, "clockout", "WAIT:1"], capture_output=True)
# 2. At the login prompt: three wrong secrets cost 1 + 2 + 4 s.
s = call(); read(s, 1.5); t0 = time.time()
for i in range(3):
    s.sendall(b"headchef\r"); read(s, 0.5); s.sendall(b"wrong%d\r" % i)
    got = ""
    while "door stays locked" not in got and time.time() - t0 < 30: got += read(s, 0.5)
    # The pause comes after the refusal is printed: the next prompt marks
    # its end, so wait for it every time (the third pause included).
    after = got.split("door stays locked", 1)[1]
    while "cook:" not in after and time.time() - t0 < 30: after += read(s, 0.5)
secs = time.time() - t0
say(secs >= 7, f"three wrong secrets over the pass took {secs:.1f} s (pauses 1 + 2 + 4)")
# 3. Log in as cook and hang up without clocking out.
s.sendall(b"cook\r"); read(s, 0.5); s.sendall(b"soup\r"); got = read(s, 3)
say("Welcome to the kitchen, cook" in got, "the caller logs in through the pass")
s.close(); time.sleep(3)
s = call(); got = read(s, 2); s.close()
say("console is in use" not in got and "share the local keyboard" in got,
    "a caller who hung up logged in left the console clocked out, so the next caller is let in")
sys.exit(fail)
PY
L=$(tr -d '\r' < "$LOG")
echo "$L" | grep "^\[pass\] refused a caller: the console is logged in" >/dev/null \
    && echo "$L" | grep "^\[pass\] the caller left logged in as cook: clocking the console out" >/dev/null \
    && echo "  ok    the refusal and the clock-out are logged" || { echo "  FAIL  pass log lines"; fail=1; }
sleep 1
mtype -i "$TESTDISK" ::/etc/logins 2>/dev/null | grep -E "refused +headchef +10\.0\.2\.2" >/dev/null \
    && mtype -i "$TESTDISK" ::/etc/logins | grep -E "ok +cook +10\.0\.2\.2" >/dev/null \
    && echo "  ok    the login record names the caller's address, not the console" || { echo "  FAIL  /etc/logins after pass logins"; fail=1; }
exit $fail

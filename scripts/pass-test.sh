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
    "plumbing dhcp" "WAIT:6" "pass 23" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }

session=$( (printf 'whoami\n'; sleep 4; printf 'larder\n'; sleep 4) \
           | timeout 25 nc 127.0.0.1 "$PORT" 2>/dev/null | tr -d '\r')

fail=0
echo "$session" | grep -q "You share the local keyboard" \
    && echo "  ok    the pass greeted the session" \
    || { echo "  FAIL  no greeting came back"; fail=1; }
echo "$session" | grep -q "headchef  (uid 0)" \
    && echo "  ok    a command typed over the network ran and answered" \
    || { echo "  FAIL  whoami's output did not come back"; fail=1; }
echo "$session" | grep -q "clusters used" \
    && echo "  ok    a second command in the same session also answered" \
    || { echo "  FAIL  larder's output did not come back"; fail=1; }
grep -qE "^\[pass\] session open" "$LOG" \
    && echo "  ok    the kernel logged the session" \
    || { echo "  FAIL  no session in the kernel log"; fail=1; }
exit $fail

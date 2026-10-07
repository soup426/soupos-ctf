#!/usr/bin/env bash
# Can soupOS ANSWER a connection instead of only making one?
#
# Starts `hatch`, then fetches from the host with curl through QEMU's hostfwd.
# Not part of smoke-test.sh because hatch blocks until a key is pressed, so the
# fetch has to happen while the guest sits in the command - the gate drives a
# straight sequence of keystrokes and has no way to act in the middle of one.
#
# Three things are checked, and the third is the one that nearly did not work:
# a file is served with the right body and length, a missing file gives 404,
# and a SECOND request succeeds - the listener does not survive the connection,
# so the server has to listen again after each client (see tcp.h).
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=18082
LOG=$(mktemp /tmp/soupos-hatch.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-hatch.XXXXXX.sock)
TESTDISK=$(mktemp /tmp/soupos-hatchdisk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:80" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5

python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "plumbing dhcp" "WAIT:6" "hatch 80" "WAIT:3" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }

fail=0

body=$(curl -s -m 15 "http://127.0.0.1:$PORT/HELLO.TXT" 2>/dev/null)
if [ "$body" = "$(printf 'Hello from soupOS!\nA warm bowl of kernel soup.')" ]; then
    echo "  ok    served HELLO.TXT with the right body"
else
    echo "  FAIL  wrong body: $(printf '%s' "$body" | head -c 60)"
    fail=1
fi

# Content-Length has to be right or a client waits for the close and then
# reports a truncated body.
len=$(curl -s -m 15 -o /dev/null -w '%{size_download}' "http://127.0.0.1:$PORT/HELLO.TXT" 2>/dev/null)
if [ "${len:-0}" = "47" ]; then
    echo "  ok    a second request also worked, 47 bytes (the listener re-armed)"
else
    echo "  FAIL  second request returned ${len:-0} bytes, expected 47"
    fail=1
fi

code=$(curl -s -m 15 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$PORT/NOPE.TXT" 2>/dev/null)
if [ "${code:-0}" = "404" ]; then
    echo "  ok    a missing file gives 404"
else
    echo "  FAIL  missing file gave ${code:-0}, expected 404"
    fail=1
fi

grep -E "^\[tcp\] accepted" "$LOG" | tail -3 | sed 's/^/  /'
exit $fail

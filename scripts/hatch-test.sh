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
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; rm -rf "${BIGDIR:-}"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
# Two files that broke hatch until v0.39.1: one past its 32 KB buffer (the
# rest went out as kernel memory) and one past 64 KB (cut by a uint16_t).
BIGDIR=$(mktemp -d /tmp/soupos-hatchbig.XXXXXX)
head -c 40000  /dev/urandom > "$BIGDIR/MID.BIN"
head -c 200000 /dev/urandom > "$BIGDIR/BIG.BIN"
mcopy -i "$TESTDISK" "$BIGDIR/MID.BIN" ::MID.BIN
mcopy -i "$TESTDISK" "$BIGDIR/BIG.BIN" ::BIG.BIN

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

# Three clients at once. Before the connection table this was impossible: the
# listener WAS the connection, so the second and third were refused. The sizes
# are the files' real ones, so a muddled response shows up as a wrong count.
# NOTE: wait for the curl PIDs specifically. A bare `wait` also waits for the
# QEMU process started above, which never exits, so the test hangs - the
# fetches themselves finish in milliseconds.
#
# The overlap is made certain, not hoped for. Until v0.43.0 the three curls
# were simply started together, and under the full check's load they
# sometimes connected far enough apart that each finished before the next
# arrived - all in slot 0, a failure of the test rather than the kernel
# (2 of 4 repeat runs used 2 slots, 2 used 3). Now a silent client is
# connected first and held: hatch sits waiting on its request, so the three
# fetches MUST each land in another slot to be served at all.
python3 -c "
import socket, sys, time
s = socket.create_connection(('127.0.0.1', int(sys.argv[1])), timeout=5)
time.sleep(1.5)
s.close()" "$PORT" >/dev/null 2>&1 &
holder=$!
sleep 0.3
par_ok=1
par_pids=""
for f in HELLO.TXT:47 RECIPE.TXT:75 SECRET.TXT:31; do
    name=${f%%:*}; want=${f##*:}
    ( got=$(curl -s -m 20 "http://127.0.0.1:$PORT/$name" 2>/dev/null | wc -c)
      [ "$got" = "$want" ] || echo "BAD $name:$got" ) >> "$LOG.par" 2>&1 &
    par_pids="$par_pids $!"
done
for pid in $par_pids; do wait "$pid"; done
wait "$holder"
if [ -s "$LOG.par" ]; then
    echo "  FAIL  parallel fetches came back wrong: $(cat "$LOG.par" | tr '\n' ' ')"
    par_ok=0; fail=1
fi
rm -f "$LOG.par"
slots=$(grep -oE "slot [0-9]+" "$LOG" | sort -u | wc -l)
if [ "$par_ok" = "1" ] && [ "$slots" -ge 3 ]; then
    echo "  ok    three clients served while a fourth held hatch, across $slots connection slots"
elif [ "$par_ok" = "1" ]; then
    echo "  FAIL  three fetches worked but all reused one slot: not concurrent"
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
for f in MID BIG; do
    curl -s -m 20 -o "$BIGDIR/$f.got" "http://127.0.0.1:$PORT/$f.BIN"
    if cmp -s "$BIGDIR/$f.BIN" "$BIGDIR/$f.got"; then
        echo "  ok    $f.BIN ($(wc -c < "$BIGDIR/$f.BIN") bytes) arrived byte for byte"
    else
        echo "  FAIL  $f.BIN differs: sent $(wc -c < "$BIGDIR/$f.BIN"), got $(wc -c < "$BIGDIR/$f.got" 2>/dev/null || echo 0)"; fail=1
    fi
done
exit $fail

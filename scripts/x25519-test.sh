#!/usr/bin/env bash
# The RFC 7748 iterated X25519 test, a thousand rounds.
#
# Lives outside the gate because it takes 1.5 s on a quiet machine and the
# arithmetic is deliberately the simple, slow kind. `sample slow` runs it; the
# fast vectors stay in plain `sample`.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-x25519.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-x25519.XXXXXX.sock)
TESTDISK=$(mktemp /tmp/soupos-x25519disk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "sample slow" "WAIT:12" >/dev/null 2>&1

fail=0
grep -q "^\[sample\] x25519 iterated 1000x ok" "$LOG" \
    && echo "  ok    a thousand chained rounds match the RFC" \
    || { echo "  FAIL  the iterated vector did not match"; fail=1; }
t=$(grep -oE "x25519 1000 rounds took [0-9]+" "$LOG" | awk '{print $5}')
[ -n "$t" ] && [ "$t" -lt 500 ] \
    && echo "  ok    they took $t ticks (under five seconds)" \
    || { echo "  FAIL  rounds took '${t:-?}' ticks"; fail=1; }
grep -qE "^\[sample\] [0-9]+ tests, 0 failed" "$LOG" \
    && echo "  ok    nothing else in sample regressed" \
    || { echo "  FAIL  sample reported failures"; fail=1; }
exit $fail

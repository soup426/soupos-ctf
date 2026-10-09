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

# One boot, `sample slow`, wait for its last line (2026-10-08: not a fixed 12 s).
measure() {
    : > "$LOG"
    cp disk.img "$TESTDISK"
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
    PID=$!
    sleep 4
    python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "sample slow" >/dev/null 2>&1
    for i in $(seq 1 120); do grep -qE "^\[sample\] [0-9]+ tests, [0-9]+ failed" "$LOG" && break; sleep 0.5; done
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
measure
fail=0
grep -q "^\[sample\] x25519 iterated 1000x ok" "$LOG" \
    && echo "  ok    a thousand chained rounds match the RFC" \
    || { echo "  FAIL  the iterated vector did not match"; fail=1; }
t=$(grep -oE "x25519 1000 rounds took [0-9]+" "$LOG" | awk '{print $5}')
# The ticks are the guest's wall time, which a busy host stretches without
# the kernel getting any slower: over the limit, it boots and measures once
# more (2026-10-09: 1790 and 1146 in two loaded full checks, ~150 alone),
# and a real slowdown fails both times.
if [ -n "$t" ] && [ "$t" -ge 1000 ]; then
    echo "  note  $t ticks under load; measuring again"
    measure
    t=$(grep -oE "x25519 1000 rounds took [0-9]+" "$LOG" | awk '{print $5}')
fi
# A guard against the arithmetic getting badly slower, not a benchmark: it
# takes about 150 ticks on a quiet machine. The limit was 500, and with 16
# busy loops competing on the host it took 504 (2026-10-08): host
# contention, not the kernel. 1000 still catches a slowdown of ~7x.
[ -n "$t" ] && [ "$t" -lt 1000 ] \
    && echo "  ok    they took $t ticks (under ten seconds)" \
    || { echo "  FAIL  rounds took '${t:-?}' ticks"; fail=1; }
grep -qE "^\[sample\] [0-9]+ tests, 0 failed" "$LOG" \
    && echo "  ok    nothing else in sample regressed" \
    || { echo "  FAIL  sample reported failures"; fail=1; }
exit $fail

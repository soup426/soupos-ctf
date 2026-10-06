#!/usr/bin/env bash
# soupOS headless smoke test — "the oracle".
#
# Boots the ISO with no display, drives the shell through the QEMU monitor
# (sendkey), and greps the COM1 serial log for the markers each subsystem
# emits. Exits non-zero on the first missing marker, so it works as a gate.
#
#   ./scripts/smoke-test.sh            # build if needed, then test
#   KEEP=1 ./scripts/smoke-test.sh     # keep the serial log for inspection
#
# Requires: qemu-system-i386, python3, and a built soupOS.iso + disk.img.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-smoke.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-smoke.XXXXXX.sock)
QEMU_PID=""

cleanup() {
    [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null
    rm -f "$MON"
    if [ "${KEEP:-0}" = "1" ]; then echo "serial log kept at $LOG"; else rm -f "$LOG"; fi
}
trap cleanup EXIT

[ -f soupOS.iso ] || { echo "soupOS.iso missing — run 'make' first"; exit 1; }
[ -f disk.img ]   || { echo "disk.img missing — run 'make disk' first"; exit 1; }

echo "booting soupOS headless..."
qemu-system-i386 \
    -drive file=disk.img,format=raw,if=ide,index=0 \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown \
    -display none \
    -serial "file:$LOG" \
    -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
QEMU_PID=$!
sleep 4

# Log in as the default headchef, then exercise preemption and ring 3.
# spin.elf runs ~3s and yieldbg ~6s, so they overlap on purpose.
python3 scripts/qemu_keys.py "$MON" \
    "headchef" "xxxxxxxx" "WAIT:1" \
    "cook systest.elf hello world" "WAIT:3" \
    "cook hello.elf" "WAIT:2" \
    "preempttest" "WAIT:8" \
    "fatstress" "WAIT:30" \
    "vgastress" "WAIT:25" \
    "vmtest" "WAIT:6" \
    "yieldbg" "cook spin.elf" "WAIT:11" >/dev/null || {
        echo "FAIL: could not drive the QEMU monitor"; exit 1; }

fail=0
check() {   # check <description> <grep-pattern>
    if grep -qE "$2" "$LOG"; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1  (no match for /$2/)"
        fail=1
    fi
}

echo "checking serial markers:"
check "boot reaches the scheduler"   '\[boot\] scheduler ok'
check "preemption armed"             '\[boot\] preemption enabled'
check "ring3 write syscall"          'USERELF: ring3 syscall write ok'
check "ring3 args syscall"           'TESTOUT args=hello world'
check "ring3 open/read syscall"      'TESTOUT read=Hello from soupOS'
check "ring3 sbrk syscall"           'TESTOUT heap='
check "ring3 yield reaches sched"    '\[u\] step'
check "background task ran"          '\[bg\] done'
check "preempt_disable works"       'PASS'
check "concurrent FAT is safe"      '\[fatstress\] PASS'
check "address spaces isolated"     '\[vmtest\] PASS'

# vgastress prints whole lines of a single repeated character from two tasks.
# Any line containing both means vga_puts was interrupted mid-string.
mixed=$(sed -n '/\[vgastress\] begin/,/\[vgastress\] end/p' "$LOG" \
        | grep -c 'A.*B\|B.*A' || true)
if [ "$mixed" = "0" ]; then
    echo "  ok    vga output not interleaved"
else
    echo "  FAIL  vga output interleaved on $mixed lines"
    fail=1
fi

# The point of preemption: the [bg] sleeper must keep ticking while a
# ring-3 program is running, i.e. [bg] markers appear on both sides of [u].
if grep -qE '\[bg\]' "$LOG" && grep -qE '\[u\]' "$LOG"; then
    first_u=$(grep -nE '\[u\] step'  "$LOG" | head -1 | cut -d: -f1)
    last_u=$( grep -nE '\[u\] step'  "$LOG" | tail -1 | cut -d: -f1)
    inner=$(awk -v a="$first_u" -v b="$last_u" 'NR>a && NR<b' "$LOG" | grep -cE '\[bg\]')
    if [ "$inner" -gt 0 ]; then
        echo "  ok    scheduler interleaved ($inner [bg] ticks during ring-3 run)"
    else
        echo "  FAIL  no [bg] ticks between the first and last [u] marker"
        fail=1
    fi
fi

# Nothing should have panicked.
if grep -qE 'KERNEL PANIC' "$LOG"; then
    echo "  FAIL  kernel panicked:"
    grep -A6 'KERNEL PANIC' "$LOG" | sed 's/^/        /'
    fail=1
else
    echo "  ok    no kernel panic"
fi

echo
if [ "$fail" = 0 ]; then echo "SMOKE TEST PASSED"; else echo "SMOKE TEST FAILED"; fi
exit $fail

#!/usr/bin/env bash
# topram-test.sh - every frame pmm gives out is mapped (v0.60.268, review
# R-K4, its first half). The identity map ran to pmm_total_pages(), a COUNT
# of usable pages, not the highest one: with the hole at 0xA0000-0xFFFFF
# below it, ~97 frames at the top of RAM (from 0x7f7f000 at -m 128M) were
# free in pmm and mapped nowhere, and the first kernel write to one
# panicked. cook spoon.elf /dev/zero > /z (any cook can) buffers the file
# in fresh frames until it reaches them: a panic in 7 s. Here it runs 20 s,
# Ctrl-C ends it, and there is no panic and the shell answers. Booted with
# -m 32M since v0.60.281 (review R-T28): v0.60.275's write cap stops the
# buffer at the disk's room (28 MB), short of the top frames at -m 128M,
# so the test could no longer fail; at 32M free RAM is under the disk's
# room, the buffer runs out of memory first ("out of memory at", required
# here so the test cannot go quiet again), and the top frames are reached.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-topram.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 32M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 90 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook spoon.elf /dev/zero > /z" "WAIT:20" "KEY:ctrl-c" "WAIT:3" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
if grep -q 'KERNEL PANIC' "$WORK/l"; then
    echo "  FAIL  writing /dev/zero to a file panicked: $(grep -m1 'fault addr' "$WORK/l")"; fail=1
else
    echo "  ok    20 s of /dev/zero into a file, no panic"
fi
grep 'out of memory at' "$WORK/l" >/dev/null && echo "  ok    the buffer ran out of memory, so it reached the top frames" \
    || { echo "  FAIL  memory never ran out: the top frames were not reached ($(grep -o '/z: [a-z ]* at [0-9]* bytes' "$WORK/l" | head -1))"; fail=1; }
[ "$(grep -c 'A warm bowl' "$WORK/l")" -ge 1 ] && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
[ "$fail" = 0 ] && echo "topram-test: PASS" || echo "topram-test: FAIL"
exit "$fail"

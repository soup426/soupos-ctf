#!/usr/bin/env bash
# writebudget-test.sh - all the write buffers together hold no more than the
# disk's room (v0.60.283, review R-K24). v0.60.275 capped each write handle at the
# disk's room, but per handle: five at once each took 28 MB, memory ran out
# under them, and everything else that needed a frame starved. One count of
# the pages every write buffer holds is kept now, capped at the disk's
# room (28 MB on the stock disk, so at -m 128M memory is never all theirs).
# Here five background spoons write /dev/zero into five files at once for
# 20 s: no write runs out of memory, the shared cap is what stops them, and
# the shell answers after.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-writebudget.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys=("headchef" "rosemary" "WAIT:1")
for i in 1 2 3 4 5; do keys+=("cook spoon.elf /dev/zero > /z$i &" "WAIT:1"); done
keys+=("WAIT:20" "wash" "WAIT:2" "pour HELLO.TXT" "UNTIL:A warm bowl")
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep 'out of memory at' "$WORK/l" >/dev/null && { echo "  FAIL  a write ran out of memory: $(grep -o '/z[0-9]: out of memory at [0-9]* bytes' "$WORK/l" | head -1)"; fail=1; }
grep 'write buffers together hold the disk.s room' "$WORK/l" >/dev/null && echo "  ok    the shared cap stopped them: $(grep -o '/z[0-9]: stops at [0-9]* bytes, the write buffers together hold the disk.s room' "$WORK/l" | head -1)" \
    || { echo "  FAIL  the shared cap never stopped a write: $(grep -o '/z[0-9]: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | tr '\n' ';')"; fail=1; }
grep 'KERNEL PANIC' "$WORK/l" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
[ "$fail" = 0 ] && echo "writebudget-test: PASS" || echo "writebudget-test: FAIL"
exit "$fail"

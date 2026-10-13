#!/usr/bin/env bash
# writecap-test.sh - a file being written holds no more RAM than the disk
# could take (v0.60.275, review R-K4, its second half). A write handle
# buffers the whole file in fresh frames until it is closed, with no cap:
# cook spoon.elf /dev/zero > /z took 121 MB, every free frame, before its
# write to a 32 MB volume failed, and every other user of memory starved
# meanwhile. The buffer stops now at the volume's free space (plus the old
# file's size): the log names that limit, under 40 MB, and memory never
# ran out. spoon reads on after its writes fail, so Ctrl-C ends it after
# 15 s (121 MB took 7); the shell answers after.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-writecap.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook spoon.elf /dev/zero > /z" "WAIT:15" "KEY:ctrl-c" "WAIT:3" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep 'out of memory at' "$WORK/l" >/dev/null && { echo "  FAIL  memory ran out first: $(grep -o '/z: out of memory at [0-9]* bytes' "$WORK/l" | head -1)"; fail=1; }
at=$(grep -o '/z: stops at [0-9]* bytes' "$WORK/l" | head -1 | grep -o '[0-9]*')
if [ -n "$at" ] && [ "$at" -lt 41943040 ]; then echo "  ok    the buffer stopped at $at bytes, the disk's room, not RAM's"
else echo "  FAIL  no stop at the disk's room: ${at:-none}"; fail=1; fi
grep 'KERNEL PANIC' "$WORK/l" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
[ "$fail" = 0 ] && echo "writecap-test: PASS" || echo "writecap-test: FAIL"
exit "$fail"

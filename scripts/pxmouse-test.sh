#!/usr/bin/env bash
# pxmouse-test.sh - the pointer in pixels (v0.60.140): it starts mid-screen
# (512,384 on the 1024x768 framebuffer), each raw count is a pixel, it is
# clamped at both edges, and the cells it has always kept move as before
# (4 counts each), read from skewer's serial line.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-pxmouse.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"; R="MON:mouse_move 240 240"
keys=("headchef" "rosemary" "WAIT:1" "skewer" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "WAIT:1" "MON:mouse_move 100 50" "WAIT:1" "skewer" "WAIT:1"
      "MON:mouse_move 7 3" "WAIT:1" "skewer" "WAIT:1"
      "$R" "$R" "$R" "$R" "$R" "WAIT:1" "skewer" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -oE '^\[mouse\] at [0-9]+,[0-9]+ .*px=[0-9]+,[0-9]+' | sed -E 's/^\[mouse\] at ([0-9]+,[0-9]+) .*px=([0-9]+,[0-9]+)/\1 \2/' > "$WORK/got"
printf '64,24 512,384\n25,12 100,50\n26,13 107,53\n127,47 1023,767\n' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    the pointer is in pixels: mid-screen at 512,384, a pixel a count (100,50 then 107,53), clamped at 0,0 and 1023,767, the cells as before"
else echo "  FAIL  the pixel pointer (cells, then pixels):"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

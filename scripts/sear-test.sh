#!/usr/bin/env bash
# sear-test.sh - sear.elf and the desktop's draw count (v0.60.152). sear
# puts 50 frames of 320x240 into a window of its own and says what each
# cost; when its window closes the desktop says how many it was given and
# how many it drew (at least one: the last, which sear leaves up a
# second). The numbers themselves are measurements, not checked here
# beyond being there; queue 49 item 4 has them.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-sear.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "cook sear.elf 320 240 50 > /S.OUT" "UNTIL:[countertop] sear:" "WAIT:1"
      "MON:mouse_move 88 0" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
out=$(mcopy -n -i "$WORK/disk.img" ::/S.OUT - 2>/dev/null)
echo "$out" | grep -E '^\[sear\] 320x240: 50 frames, fill [0-9]+ us, put [0-9]+ us, [0-9]+ frames/s from here$' >/dev/null \
    || { echo "        sear said: [$out]"; fail=1; }
drawn=$(tr -d '\r' < "$WORK/serial.log" | sed -n 's/^\[countertop\] sear: 50 puts, \([0-9]*\) drawn, [0-9]* us a draw$/\1/p')
[ -n "$drawn" ] && [ "$drawn" -ge 1 ] && [ "$drawn" -le 50 ] || { echo "        the desktop said: $(grep '\[countertop\] sear' "$WORK/serial.log")"; fail=1; }
grep -x '\[countertop\] down' <(tr -d '\r' < "$WORK/serial.log") >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    sear.elf: 50 frames of 320x240 timed from ring 3 ($out); the desktop counted 50 puts and drew $drawn" \
    || echo "  FAIL  sear.elf and the draw count"
exit $fail

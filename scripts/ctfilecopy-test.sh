#!/usr/bin/env bash
# ctfilecopy-test.sh - copying a file by dragging it between files windows
# (v0.60.177), as saucier. Files 1 shows /home/saucier, Files 2 (a step
# down and left) shows /. HELLO.TXT dragged from 2 onto 1 is copied there,
# byte for byte; dragged again it becomes "copy HELLO.TXT"; a file dragged
# from the home onto / is refused (saucier may not write /). The disk
# after and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctfilecopy.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
DRAG=("MON:mouse_button 1" "WAIT:1" "MON:mouse_move 135 58" "WAIT:1" "MON:mouse_move 135 58" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Files 1 at 604,60 (rows from y 112), Files 2 at 576,88 (rows from y 140): HELLO.TXT is its row 2, y 176-193.
# The last drag grabs a row of Files 1 (which raises it), so it lets go at 590,400, where only Files 2 is.
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "clockout" "WAIT:2"
      "saucier" "basil" "WAIT:2" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 350 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 350 126" "WAIT:1" "$TWICE" "WAIT:2" "$TWICE" "WAIT:2"
      "MON:mouse_move -350 -126" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 350 172" "WAIT:1" "${DRAG[@]}"
      "MON:mouse_move -270 -116" "WAIT:1" "${DRAG[@]}"
      "MON:mouse_move 0 -162" "WAIT:1" "MON:screendump $WORK/s1.ppm" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move -190 131" "WAIT:1" "MON:mouse_move -190 131" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:mouse_move -300 -388" "WAIT:1" "${CLICK[@]}" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT "$WORK/orig" 2>/dev/null
for f in "HELLO.TXT" "copy HELLO.TXT"; do
    mcopy -n -i "$WORK/disk.img" "::/home/saucier/$f" "$WORK/got" 2>/dev/null && cmp -s "$WORK/orig" "$WORK/got" \
        || { echo "        /home/saucier/$f is not HELLO.TXT's bytes"; fail=1; }
done
mdir -i "$WORK/disk.img" "::/copy HELLO.TXT" >/dev/null 2>&1 && { echo "        a copy reached /"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (copied|may not copy|could not|only files)' "$WORK/log" | sed 's/ (.* bytes)$//' > "$WORK/events"
printf '%s\n' '[countertop] copied /HELLO.TXT to /home/saucier/HELLO.TXT' '[countertop] copied /HELLO.TXT to /home/saucier/copy HELLO.TXT' \
       '[countertop] may not copy /home/saucier/HELLO.TXT to /copy HELLO.TXT' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    copying by dragging: HELLO.TXT into saucier's home byte for byte, again as copy HELLO.TXT; into / refused" \
    || echo "  FAIL  copying between files windows"
exit $fail

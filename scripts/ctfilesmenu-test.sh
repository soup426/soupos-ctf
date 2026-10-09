#!/usr/bin/env bash
# ctfilesmenu-test.sh - new bowls and removing files from the files window
# (v0.60.172), as saucier. In /home/saucier a right click makes a bowl;
# note.txt's "remove" asks again ("really remove note.txt?") and then
# removes it; the new, empty bowl is removed too. In / both are refused by
# the shell's rule (saucier may not write there). The listing read back
# against the font, the disk after, and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctfilesmenu.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
RIGHT=("MON:mouse_button 4" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
# the files window at 604,60: rows from y 112, 18 each; its menu where the right click was, rows of 22
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "clockout" "WAIT:2"
      "saucier" "basil" "WAIT:2" "stir note.txt hello" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 350 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 350 126" "WAIT:1" "$TWICE" "WAIT:2" "$TWICE" "WAIT:2"
      "MON:mouse_move 100 162" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move 10 13" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move -110 -156" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move 10 35" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s2.ppm" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move -10 -53" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move 10 35" "WAIT:1" "${CLICK[@]}" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move -10 -53" "WAIT:1" "$TWICE" "WAIT:2" "$TWICE" "WAIT:2"
      "MON:mouse_move 0 -26" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move 10 13" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move -10 49" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move 10 35" "WAIT:1" "${CLICK[@]}" "${CLICK[@]}"
      "MON:mouse_move -422 -180" "WAIT:1" "${CLICK[@]}" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re
work, fonth = sys.argv[1], sys.argv[2]
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127):
    shape.setdefault(font[c][:14], chr(c))
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
def word(at, x, y, n, ink):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(x + c*8 + b, y + j) in ink) for j in range(14)), '?')
                   for c in range(n)).rstrip()
ROWINK = {0x202020, 0x1E4E8C, 0xF0F0F0}
rows = lambda at: [word(at, 615, 112 + 18 * i, 12, ROWINK) for i in range(3)]
bad = []
r1 = rows(ppm('s1'))
if r1 != ['..', 'bowl/', 'note.txt']: bad.append(f'after new bowl: {r1}')
ask = word(ppm('s2'), 718, 159 + 22 + 3, 25, {0xC0504D})    # from its second letter: the pointer is on the first
if ask != 'eally remove note.txt?': bad.append(f'the second line asks "r{ask}"')
r3 = rows(ppm('s3'))
if r3[:1] != ['..'] or any(r3[1:]): bad.append(f'after both removed: {r3}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
mdir -i "$WORK/disk.img" ::/home/saucier 2>/dev/null | grep -iE 'note|bowl' && { echo "        /home/saucier still holds them"; fail=1; }
mdir -i "$WORK/disk.img" ::/HELLO.TXT >/dev/null 2>&1 || { echo "        /HELLO.TXT is gone"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (new bowl|removed|may not|not empty|could not)' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] new bowl /home/saucier/bowl' '[countertop] removed /home/saucier/note.txt' '[countertop] removed /home/saucier/bowl' \
       '[countertop] may not make /bowl' '[countertop] may not remove /HELLO.TXT' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the files menu: a bowl made and note.txt removed (asked twice) in saucier's home, the bowl removed; both refused in /" \
    || echo "  FAIL  the files window's menu"
exit $fail

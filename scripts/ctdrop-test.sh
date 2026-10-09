#!/usr/bin/env bash
# ctdrop-test.sh - a file dragged onto a terminal (v0.60.163). With
# "pour " typed in Terminal 1, HELLO.TXT dragged from the files window and
# let go over it types '/HELLO.TXT' there, and Enter pours it; a row let
# go over the background types nothing anywhere. From a screendump of
# the terminal's cells and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctdrop.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Files at 604,60: row 1 (HELLO.TXT) at y 138, row 2 at 156; Terminal 1 at 60,60
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "TYPE:pour " "WAIT:1"
      "MON:mouse_move 150 0" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 350 126" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move -200 81" "WAIT:1" "MON:mouse_move -200 81" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "KEY:ret" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 500 -144" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move -300 400" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:mouse_move -212 -544" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
d = open(f'{work}/s1.ppm', 'rb').read(); i = 0; words = []
while len(words) < 4:
    while d[i:i+1].isspace(): i += 1
    j = i
    while not d[j:j+1].isspace(): j += 1
    words.append(d[i:j]); i = j
px = d[i + 1:]; W = int(words[1])
at = lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
t = [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(61 + c*8 + b, 83 + r*16 + j) != 0) for j in range(14)), '?')
             for c in range(80)).rstrip() for r in range(25)]
bad = []
if not any(r.endswith("> pour '/HELLO.TXT'") for r in t): bad.append('no "pour \'/HELLO.TXT\'" on the prompt: ' + ' | '.join(r for r in t if r)[:300])
if 'Hello from soupOS!' not in t: bad.append('the file was not poured')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] dropped' "$WORK/log" > "$WORK/events"
printf '[countertop] dropped /HELLO.TXT on Terminal 1\n' | cmp -s - "$WORK/events" \
    || { echo "        the drops: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    a file dragged onto a terminal: its quoted path typed after 'pour ', Enter pours it; let go over the background, nothing" \
    || echo "  FAIL  dragging a file onto a terminal"
exit $fail

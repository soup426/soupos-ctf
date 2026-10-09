#!/usr/bin/env bash
# ctmax-test.sh - maximise and restore (v0.60.161). A double click on
# Welcome's title bar fills the space between the bars with it, another
# puts it back; a terminal maximised keeps its 80x25 cells, centred in the
# frame, takes a command there, and is put back with them intact. From
# screendumps (the title bar across the screen, cells read against the
# font at the centred origin and back at the window's own) and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctmax.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 300 111" "WAIT:1" "$TWICE" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 0 -76" "WAIT:1" "$TWICE" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move -100 -23" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 100 58" "WAIT:1" "$TWICE" "WAIT:2" "slurp maxed" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 0 -35" "WAIT:1" "$TWICE" "WAIT:2" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move -12 -23" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def term(at, ox, oy):
    return [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(ox + c*8 + b, oy + r*16 + j) != 0) for j in range(14)), '?')
                    for c in range(80)).rstrip() for r in range(25)]
FOC, WEL = 0x3A6EA5, 0xF4F1EA
bad = []
s1 = ppm('s1')
if s1(1000, 30) != FOC: bad.append(f's1: no title bar across the top ({s1(1000,30):06x} at 1000,30)')
if s1(1000, 600) != WEL: bad.append(f"s1: Welcome's body does not fill the screen ({s1(1000,600):06x})")
s2 = ppm('s2')
if s2(1000, 30) == FOC or s2(1000, 600) == WEL: bad.append('s2: Welcome still maximised')
if s2(300, 105) != FOC: bad.append(f"s2: Welcome's title bar not back at 120,100 ({s2(300,105):06x})")
# maximised: frame 0,24 1024x716; cells 640x400 centred: 1 + 191, 24 + 1 + 22 + 146
t3 = term(ppm('s3'), 192, 193)
if not any(r.endswith('> slurp maxed') for r in t3) or 'maxed' not in t3: bad.append('s3: the maximised terminal: ' + ' | '.join(r for r in t3 if r)[:300])
t4 = term(ppm('s4'), 61, 83)
if not any(r.endswith('> slurp maxed') for r in t4): bad.append('s4: the restored terminal: ' + ' | '.join(r for r in t4 if r)[:300])
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (maximise|restore)' "$WORK/log" > "$WORK/events"
printf '[countertop] maximise Welcome\n[countertop] restore Welcome\n[countertop] maximise Terminal 1\n[countertop] restore Terminal 1\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    maximise and restore: Welcome fills the screen and comes back; a terminal keeps its cells, centred, takes a command, and comes back with it" \
    || echo "  FAIL  maximise and restore"
exit $fail

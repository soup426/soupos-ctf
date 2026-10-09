#!/usr/bin/env bash
# frost-test.sh - windows for ring-3 programs (v0.60.146), through
# frost.elf, the paint program. From the console it says it needs the
# desktop. In a countertop terminal it opens a window of its own: a drag
# draws a black line, a click picks red, another drag a red diagonal (read
# back from a screendump, pixel by pixel); c clears it; the close box asks
# it to close and it does (its strokes reported in the terminal, read back
# against the font); run again, Ctrl-C in the terminal ends it and the
# desktop takes its window down; Leave ends the desktop.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-frost.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1")
# frost's window: 240,70, its body at 241,93; the canvas below a 24-pixel palette
keys=("headchef" "rosemary" "WAIT:1" "cook frost.elf" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook frost.elf" "WAIT:2"
      "MON:mouse_move 101 181" "MON:mouse_button 1" "MON:mouse_move 50 0" "MON:mouse_move 50 0" "MON:mouse_button 0" "WAIT:1"
      "MON:mouse_move -92 -88" "${CLICK[@]}"
      "MON:mouse_move -8 138" "MON:mouse_button 1" "MON:mouse_move 40 40" "MON:mouse_move 40 40" "MON:mouse_button 0" "WAIT:1"
      "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "TYPE:c" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move 249 -241" "WAIT:1" "${CLICK[@]}" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "cook frost.elf" "WAIT:2" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move -530 -12" "WAIT:1" "${CLICK[@]}" "KEY:ctrl-c" "WAIT:2" "MON:screendump $WORK/s5.ppm" "WAIT:1"
      "MON:mouse_move 188 -58" "WAIT:1" "${CLICK[@]}" "WAIT:2" "hash FBHELLO" "UNTIL:@soupOS:")
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
    rows = []
    for r in range(25):
        line = ''
        for c in range(80):
            g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + r*16 + j) != 0) for j in range(14))
            line += shape.get(g, '?')
        rows.append(line.rstrip())
    return rows
INK, RED, WHITE, FRAME = 0x202020, 0xC0392B, 0xFFFFFF, 0x303840
BX, BY = 241, 93                                 # frost's body on the screen
bad = []
def want(at, x, y, exp, what):
    if at(x, y) != exp: bad.append(f'{what}: {at(x,y):06x} at {x},{y}, not {exp:06x}')
s1 = ppm('s1')
for x in range(65, 160, 10): want(s1, BX + x, BY + 100, INK, 's1 the black stroke')
for t in range(5, 80, 10): want(s1, BX + 60 + t, BY + 150 + t, RED, 's1 the red stroke')
want(s1, BX + 110, BY + 120, WHITE, 's1 the canvas between them'); want(s1, BX + 46, BY + 1, FRAME, 's1 red framed in the palette')
s2 = ppm('s2')
want(s2, BX + 110, BY + 100, WHITE, 's2 cleared (the black stroke)'); want(s2, BX + 100, BY + 190, WHITE, 's2 cleared (the red one)')
s3 = ppm('s3')
want(s3, 500, 300, 0x000000, 's3 the window gone, Terminal 1 under it')
t = term(s3, 61, 83)
for w in ('[frost] stroke 1 from 60,100 to 160,100 in colour 0', '[frost] colour 1',
          '[frost] stroke 2 from 60,150 to 140,230 in colour 1', '[frost] cleared',
          '[frost] closed after 2 strokes', 'frost: 2 strokes'):
    if not any(w in r for r in t): bad.append(f'Terminal 1: no "{w}" in ' + ' | '.join(r for r in t if r)[:400])
want(ppm('s4'), BX + 110, BY + 120, WHITE, 's4 frost open again')
want(ppm('s5'), 500, 300, 0x000000, 's5 its window taken down after Ctrl-C')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x '\[frost\] no window' "$WORK/log" >/dev/null || { echo "        frost from the console did not say it needs the desktop"; fail=1; }
grep -E '^\[countertop\] (open frost|close asked|program|down)' "$WORK/log" | sed 's/ for pid [0-9]*//' > "$WORK/events"
printf '[countertop] open frost at 240,70\n[countertop] close asked of frost\n[countertop] program closed frost\n[countertop] open frost at 240,70\n[countertop] program ended, close frost\n[countertop] down\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    frost.elf: no desktop, no window; on it, drags draw black then red, c clears, the close box closes it, Ctrl-C ends it and its window goes" \
    || echo "  FAIL  frost.elf and program windows"
exit $fail

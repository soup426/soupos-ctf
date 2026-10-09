#!/usr/bin/env bash
# ctsnap-test.sh - snap to halves (v0.60.167). Welcome dragged by its title
# to the left edge fills the left half; on to the right edge (through the
# middle, where it is its own size for a moment), the right half; back
# into the middle, its own size under the pointer. Long moves go in steps
# of 300 at most: a single mouse_move of 1100 did not arrive whole. A terminal
# snapped left becomes 63x43 cells (80 do not fit 512 pixels), takes a
# command there, and dragged off again is 80x25 with it intact. From
# screendumps (cells read against the font) and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctsnap.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 300 105" "WAIT:1" "MON:mouse_button 1" "WAIT:1"
      "MON:mouse_move -400 0" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 300 0" "MON:mouse_move 300 0" "MON:mouse_move 300 0" "MON:mouse_move 200 0" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move -511 0" "WAIT:1" "MON:mouse_button 0" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move -312 -93" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 100 58" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move -400 0" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "slurp sixty-three wide" "WAIT:2" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move 200 -35" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 300 100" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:screendump $WORK/s5.ppm" "WAIT:1"
      "MON:mouse_move -212 -123" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def term(at, ox, oy, cols, rows):
    return [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(ox + c*8 + b, oy + r*16 + j) != 0) for j in range(14)), '?')
                    for c in range(cols)).rstrip() for r in range(rows)]
WEL = 0xF4F1EA
bad = []
s1 = ppm('s1')
if s1(100, 600) != WEL or s1(900, 600) == WEL: bad.append(f'snap left: {s1(100,600):06x} at 100,600, {s1(900,600):06x} at 900,600')
s2 = ppm('s2')
if s2(900, 600) != WEL or s2(100, 600) == WEL: bad.append(f'snap right: {s2(900,600):06x} at 900,600, {s2(100,600):06x} at 100,600')
s3 = ppm('s3')
if s3(700, 300) != WEL or s3(100, 600) == WEL or s3(900, 600) == WEL: bad.append('unsnapped: not its own size about 305,94')   # 700,300: its body, clear of text
# Terminal 1 snapped left: frame 0,24 512x716; 63x43 cells centred: 1 + 3, 24 + 1 + 22 + 2
t4 = term(ppm('s4'), 4, 49, 63, 43)
if not any(r.endswith('> slurp sixty-three wide') for r in t4) or 'sixty-three wide' not in t4:
    bad.append('snapped terminal: ' + ' | '.join(r for r in t4 if r)[:300])
# dragged off at 500,135: 80x25 again, its frame at 500 - 321, 135 - 11
t5 = term(ppm('s5'), 180, 147, 80, 25)
if not any(r.endswith('> slurp sixty-three wide') for r in t5): bad.append('unsnapped terminal: ' + ' | '.join(r for r in t5 if r)[:300])
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (snap|unsnap)' "$WORK/log" > "$WORK/events"
# the steps to the right edge pass through the middle: unsnapped there, then snapped right
printf '[countertop] snap left Welcome\n[countertop] unsnap Welcome\n[countertop] snap right Welcome\n[countertop] unsnap Welcome\n[countertop] snap left Terminal 1\n[countertop] unsnap Terminal 1\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    snap to halves: left, right, and back to its own size; a terminal snapped is 63x43 and 80x25 again after, its lines kept" \
    || echo "  FAIL  snap to halves"
exit $fail

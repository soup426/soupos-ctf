#!/usr/bin/env bash
# ctrack-test.sh - the spice rack (v0.60.188). Opened from the Kitchen menu
# (and again from the background menu, which raises the same one): the
# forest swatch, the cloche + (10 to 15 min) then never, the zone - twice,
# 12-hour, and the picture's none, each logged as kept in /countertop; the
# rack then reads UTC-2, never and (none), the bar's clock has am/pm, and
# /countertop holds exactly those settings.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctrack.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'picture /nowhere.bmp\n' > "$WORK/settings"
mcopy -i "$WORK/disk.img" "$WORK/settings" ::/countertop
mapfile -d '' -t keys < <(python3 - "$WORK" <<'PY'
import sys
work = sys.argv[1]
out = ['headchef', 'rosemary', 'WAIT:1', 'countertop', 'WAIT:2'] + ['MON:mouse_move -150 -150'] * 8
pos = [0, 0]
def go(x, y):
    while pos != [x, y]:
        dx = max(-150, min(150, x - pos[0])); dy = max(-150, min(150, y - pos[1]))
        out.append(f'MON:mouse_move {dx} {dy}'); pos[0] += dx; pos[1] += dy
    out.append('WAIT:1')
click = ['MON:mouse_button 1', 'WAIT:1', 'MON:mouse_button 0', 'WAIT:2']
BX, BY = 333, 107                                  # the rack's body: the window at 332,84
go(420, 12); out += click; go(420, 206); out += click        # Kitchen, then its 8th row: spice rack
for x, y in ((214, 20), (232, 102), (318, 102), (116, 142), (116, 142), (234, 182), (318, 62)):
    go(BX + x, BY + y); out += click
go(900, 650); out += ['MON:mouse_button 2', 'WAIT:1', 'MON:mouse_button 0', 'WAIT:2']   # the background menu at 800,592
go(810, 592 + 2 + 5 * 24 + 12); out += click                 # its 6th: spice rack again
go(1000, 700); out += [f'MON:screendump {work}/s1.ppm', 'WAIT:1']
print('\0'.join(out), end='')
PY
)
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
word = lambda x, y, n, ink: ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(x + c*8 + b, y + j) == ink) for j in range(14)), '?') for c in range(n)).rstrip()
BX, BY = 333, 107
bad = []
for (x, y, n, want) in ((104, 54, 6, '(none)'), (138, 94, 5, 'never'), (138, 134, 5, 'UTC-2')):
    got = word(BX + x, BY + y, n, 0x202020)
    if got != want: bad.append(f'the rack reads "{got}", not "{want}"')
ampm = word(952 + 48, 4, 2, 0xF0F0F0)
if ampm not in ('am', 'pm'): bad.append(f'the bar\'s clock ends "{ampm}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
mcopy -n -i "$WORK/disk.img" ::/countertop "$WORK/after" 2>/dev/null
printf 'background 2\nidle 0\nzone -2\nclock 12\n' | cmp -s - "$WORK/after" || { echo "        /countertop: $(tr '\n' ';' < "$WORK/after")"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] spice rack' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] spice rack open' '[countertop] spice rack: backdrop forest, kept in /countertop' \
       '[countertop] spice rack: cloche 900 s, kept in /countertop' '[countertop] spice rack: cloche 0 s, kept in /countertop' \
       '[countertop] spice rack: zone -1, kept in /countertop' '[countertop] spice rack: zone -2, kept in /countertop' \
       '[countertop] spice rack: clock 12, kept in /countertop' '[countertop] spice rack: picture none, kept in /countertop' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the spice rack: a backdrop, the cloche's time, the zone, the clock and the picture clicked, each kept; read back on the rack and the bar; one rack" \
    || echo "  FAIL  the spice rack"
exit $fail

#!/usr/bin/env bash
# ctplatter-test.sh - pictures from the files window (v0.60.186). In /pics
# (made on the host, the root's first bowl): PIC.BMP 300x200 double-clicked
# opens a platter, its pixels compared; TALL.BMP 200x700, taller than a
# platter shows, scrolls 48 rows with a notch of the wheel (compared again);
# the files menu on PIC.BMP has "use as background", which redraws the
# background with it and keeps "picture /pics/PIC.BMP" in /countertop.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctplatter.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
python3 - "$WORK" <<'PY'
import sys, struct
work = sys.argv[1]
def bmp(name, w, h, pix):
    stride = (w * 3 + 3) & ~3
    data = b''.join(b''.join(struct.pack('BBB', pix(x, y) & 255, pix(x, y) >> 8 & 255, pix(x, y) >> 16) for x in range(w)) + b'\0' * (stride - 3 * w)
                    for y in range(h - 1, -1, -1))
    open(f'{work}/{name}', 'wb').write(b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
                                       + struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0) + data)
bmp('PIC.BMP', 300, 200, lambda x, y: (x * 7 % 256) << 16 | (y * 5 % 256) << 8 | (x ^ y) & 255)
bmp('TALL.BMP', 200, 700, lambda x, y: (y % 256) << 16 | (y // 256 * 80) << 8 | x & 255)
PY
mmd -i "$WORK/disk.img" ::/pics
for f in PIC.BMP TALL.BMP; do mcopy -i "$WORK/disk.img" "$WORK/$f" "::/pics/$f"; done
# the moves, from a tracked position, in steps the mouse takes whole; NUL
# between the items, since a double click is one item with newlines in it
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
twice = ['MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0', 'WAIT:3']
go(350, 12); out += click                      # Files at /, its window at 604,60, rows from y 112
go(700, 120); out += twice                     # pics (row 0)
go(700, 138); out += twice                     # PIC.BMP (row 1): a platter at 40,40
go(1000, 600); out += [f'MON:screendump {work}/s1.ppm', 'WAIT:1']
go(700, 156); out += twice                     # TALL.BMP (row 2): a platter at 68,68
go(150, 400); out += [f'MON:screendump {work}/s2.ppm', 'WAIT:1', 'MONQ:mouse_move 0 0 -1', 'WAIT:2']
go(1000, 600); out += [f'MON:screendump {work}/s3.ppm', 'WAIT:1']
go(700, 138); out += ['MON:mouse_button 2', 'WAIT:1', 'MON:mouse_button 0', 'WAIT:2']   # the files menu at 700,138
go(710, 200); out += click                     # its third entry
go(1000, 600); out += [f'MON:screendump {work}/s4.ppm', 'WAIT:1']
print('\0'.join(out), end='')
PY
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
work = sys.argv[1]
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
pic = lambda x, y: (x * 7 % 256) << 16 | (y * 5 % 256) << 8 | (x ^ y) & 255
tall = lambda x, y: (y % 256) << 16 | (y // 256 * 80) << 8 | x & 255
bad = []
def cmp(n, s, bx, by, w, h, f, top=0, skip=lambda x, y: False):
    wrong = sum(s(bx + x, by + y) != f(x, y + top) for y in range(h) for x in range(w) if not skip(bx + x, by + y))
    if wrong: bad.append(f'{n}: {wrong} of {w * h} wrong')
cmp('s1 PIC', ppm('s1'), 41, 63, 300, 200, pic)
s3 = ppm('s3')
cmp('s3 TALL', s3, 69, 91, 200, 636, tall, top=48)
cmp('s2 TALL', ppm('s2'), 69, 91, 200, 636, tall, skip=lambda x, y: 150 <= x < 161 and 400 <= y < 417)
# the background picture, centred at 362,282, where nothing covers it (between the platters and Pantry)
cmp('s4 background', ppm('s4'), 362 + 0, 330, 58, 150, lambda x, y: pic(x, y + 48))
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
mcopy -n -i "$WORK/disk.img" ::/countertop "$WORK/settings" 2>/dev/null
grep -qx 'picture /pics/PIC.BMP' "$WORK/settings" 2>/dev/null || { echo "        /countertop: $(tr '\n' ';' < "$WORK/settings" 2>/dev/null)"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (platter|TALL.BMP scrolled|picture /pics)' "$WORK/log" | sed -E 's/, read in .*//' > "$WORK/events"
printf '%s\n' '[countertop] platter /pics/PIC.BMP: 300x200' '[countertop] platter /pics/TALL.BMP: 200x700' '[countertop] TALL.BMP scrolled to 48' \
       '[countertop] picture /pics/PIC.BMP: 300x200' '[countertop] picture /pics/PIC.BMP kept in /countertop' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    pictures from the files window: PIC.BMP in a platter pixel for pixel; TALL.BMP scrolled 48 by the wheel; use as background draws it and keeps it in /countertop" \
    || echo "  FAIL  pictures from the files window"
exit $fail

#!/usr/bin/env bash
# ctpicture-test.sh - a picture for the background (v0.60.182). BMPs made
# here: 37x23 bottom-up (rows padded to 4 bytes), 20x10 top-down, and a
# whole 1024x768. Each named in /countertop ("picture <path>", by stir),
# the desktop opened, and the screendump compared pixel for pixel where
# the picture should be, centred between the bars; outside it the
# gradient. HELLO.TXT named instead is refused (logged) and the gradient
# stays. The read and draw times of the big one are logged.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctpicture.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
python3 - "$WORK" <<'PY'
import sys, struct, random
work = sys.argv[1]
def bmp(name, w, h, pix, topdown=False):
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in (range(h) if topdown else range(h - 1, -1, -1)):
        r = b''.join(struct.pack('BBB', pix(x, y) & 255, pix(x, y) >> 8 & 255, pix(x, y) >> 16) for x in range(w))
        rows.append(r + b'\0' * (stride - 3 * w))
    data = b''.join(rows)
    hdr = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54) + struct.pack('<IiiHHIIiiII', 40, w, -h if topdown else h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    open(f'{work}/{name}', 'wb').write(hdr + data)
random.seed(7)
sky = [[random.randrange(1 << 24) for x in range(37)] for y in range(23)]
bmp('SKY.BMP', 37, 23, lambda x, y: sky[y][x])
open(f'{work}/sky.txt', 'w').write(repr(sky))
bmp('TOP.BMP', 20, 10, lambda x, y: (x * 12) << 16 | (y * 25) << 8 | 0x40)
bmp('BIG.BMP', 1024, 768, lambda x, y: (x // 4) << 16 | (y // 3) << 8 | ((x + y) & 255))
PY
for f in SKY.BMP TOP.BMP BIG.BMP; do mcopy -i "$WORK/disk.img" "$WORK/$f" "::/$f"; done
# Pantry opens over the middle of the screen, focused: Alt+F4 puts it away first
cyc() { printf '%s\n' "stir /countertop picture /$1" "WAIT:1" "countertop" "WAIT:3" "KEY:alt-f4" "WAIT:2" "MON:screendump $WORK/$2.ppm" "WAIT:1" "KEY:esc" "WAIT:2"; }
mapfile -t keys < <(printf '%s\n' "headchef" "rosemary" "WAIT:1" "MON:mouse_move -2000 -2000" "WAIT:1"; cyc SKY.BMP s1; cyc TOP.BMP s2; cyc BIG.BMP s3; cyc HELLO.TXT s4)
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
W, H, BAR, TASK = 1024, 768, 24, 28
at = lambda w, h: ((W - w) // 2, max(BAR, BAR + (H - BAR - TASK - h) // 2))
bad = []
sky = eval(open(f'{work}/sky.txt').read())
def same(n, w, h, pix):
    s = ppm(n); x0, y0 = at(w, h)
    wrong = sum(s(x0 + x, y0 + y) != pix(x, y) for y in range(h) for x in range(w))
    if wrong: bad.append(f'{n}: {wrong} of {w * h} pixels wrong at {x0},{y0}')
    return s, x0, y0
s1, x0, y0 = same('s1', 37, 23, lambda x, y: sky[y][x])
if s1(x0 - 1, y0) != s1(x0 + 37, y0): bad.append('s1: not the gradient either side')
same('s2', 20, 10, lambda x, y: (x * 12) << 16 | (y * 25) << 8 | 0x40)
s3 = ppm('s3')                                     # the big one, where no window or icon is: rows from the top of the picture
wrong = sum(s3(x, y) != ((x // 4) << 16 | ((y - BAR) // 3) << 8 | ((x + y - BAR) & 255))
            for y in range(560, 700, 7) for x in range(800, 1000, 3))
if wrong: bad.append(f's3: {wrong} sampled pixels wrong')
s4 = ppm('s4')
if s4(600, 600) != s4(900, 600) or s4(600, 600) == s4(600, 300): bad.append('s4: not the gradient')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] picture ' "$WORK/log" | sed -E 's/read in [0-9]+ ms, drawn in [0-9.]+ ms/read, drawn/' > "$WORK/events"
printf '%s\n' '[countertop] picture /SKY.BMP: 37x23, read, drawn' '[countertop] picture /TOP.BMP: 20x10, read, drawn' \
       '[countertop] picture /BIG.BMP: 1024x768, read, drawn' '[countertop] picture /HELLO.TXT: not a 24-bit BMP, the gradient instead' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
big=$(grep -oE 'BIG\.BMP: 1024x768, read in [0-9]+ ms, drawn in [0-9.]+ ms' "$WORK/log")
[ "$fail" = 0 ] && echo "  ok    a picture for the background: 37x23 bottom-up and 20x10 top-down pixel for pixel, centred; 1024x768 too (${big#BIG.BMP: 1024x768, }); HELLO.TXT refused, the gradient" \
    || echo "  FAIL  a picture for the background"
exit $fail

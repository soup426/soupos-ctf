#!/usr/bin/env bash
# swirl-test.sh - swirl.elf (v0.60.153), the Mandelbrot set in 16.16 fixed
# point, against the same integer arithmetic done here in Python: every
# one of its 320x240 pixels in a screendump, for the first view and for
# the view after a click zooms in on a point; then q ends it (two views,
# its window taken down, 60 puts of eight rows each).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-swirl.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
# swirl's window: 240,70, its body at 241,93; the click lands on body pixel 100,60
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "cook swirl.elf" "WAIT:4" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 141 141" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:4" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:q" "WAIT:3" "MON:mouse_move -53 -141" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
work = sys.argv[1]
SW, SH, MAXIT = 320, 240, 64
def colour(it):
    if it >= MAXIT: return 0
    return ((it * 4) & 255) << 16 | ((it * 11 + 40) & 255) << 8 | ((255 - it * 3) & 255)
def escape(cx, cy):                               # swirl.c's escape(), integer for integer
    x = y = 0
    for it in range(MAXIT):
        x2, y2 = (x * x) >> 16, (y * y) >> 16
        if x2 + y2 > (4 << 16): return it
        y = ((2 * x * y) >> 16) + cy
        x = x2 - y2 + cx
    return MAXIT
def view(cx, cy, step):
    return [[colour(escape(cx + (c - SW // 2) * step, cy + (r - SH // 2) * step)) for c in range(SW)] for r in range(SH)]
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
BX, BY = 241, 93
cx, cy, step = -(1 << 15), 0, (3 << 16) // SW
bad = []
for name, (vx, vy, vs) in (('s1', (cx, cy, step)), ('s2', (cx + (100 - SW // 2) * step, cy + (60 - SH // 2) * step, step >> 1))):
    want, at = view(vx, vy, vs), ppm(name)
    wrong = [(c, r) for r in range(SH) for c in range(SW) if at(BX + c, BY + r) != want[r][c]]
    # the pointer is drawn over the body in s2 (at the click): its 11x18 box may differ
    if name == 's2': wrong = [(c, r) for c, r in wrong if not (100 <= c < 111 and 60 <= r < 78)]
    if wrong:
        c, r = wrong[0]
        bad.append(f'{name}: {len(wrong)} pixels differ, first {c},{r}: {at(BX + c, BY + r):06x}, not {want[r][c]:06x}')
    inside = sum(1 for row in want for p in row if p == 0)
    if not 0 < inside < SW * SH: bad.append(f'{name}: the reference view is all one thing ({inside} inside)')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x '\[countertop\] program closed swirl' "$WORK/log" >/dev/null || { echo "        swirl's window was not closed by swirl: $(grep 'countertop\] program' "$WORK/log")"; fail=1; }
grep -E '^\[countertop\] swirl: 60 puts, [0-9]+ drawn' "$WORK/log" >/dev/null || { echo "        not 60 puts: $(grep 'countertop\] swirl' "$WORK/log")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    swirl.elf: every pixel of the first view and of a click's zoom matches the host's fixed-point Mandelbrot; q ends it, its window gone" \
    || echo "  FAIL  swirl.elf"
exit $fail

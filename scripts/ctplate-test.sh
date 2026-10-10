#!/usr/bin/env bash
# ctplate-test.sh - a picture of the screen (v0.60.185). Print Screen on
# the headchef's desktop saves /plate1.bmp: a 1024x768 24-bit BMP whose
# pixels are the screen's (QEMU's screendump of the same moment, below the
# bar, where only the clock and the note move; the pointer is not in the
# plate, so the only difference allowed is one arrow's worth). A second press is plate2.bmp. plate1.bmp named as the picture in
# /countertop is drawn back as the background.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctplate.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "KEY:print" "WAIT:4" "MON:screendump $WORK/s1.ppm" "WAIT:1" "KEY:print" "WAIT:4" "KEY:esc" "WAIT:2"
      "stir /countertop picture /plate1.bmp" "WAIT:1" "countertop" "WAIT:4" "MON:screendump $WORK/s2.ppm" "WAIT:1" "KEY:esc" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/plate1.bmp "$WORK/plate1.bmp" 2>/dev/null || { echo "        no /plate1.bmp"; fail=1; }
mdir -i "$WORK/disk.img" ::/plate2.bmp >/dev/null 2>&1 || { echo "        no /plate2.bmp"; fail=1; }
[ "$fail" = 0 ] && python3 - "$WORK" <<'PY' || fail=1
import sys, struct
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
b = open(f'{work}/plate1.bmp', 'rb').read()
bad = []
off, = struct.unpack_from('<I', b, 10)
w, h, planes, bits, comp = struct.unpack_from('<iiHHI', b, 18)
if b[:2] != b'BM' or (w, h, planes, bits, comp) != (1024, 768, 1, 24, 0) or len(b) != 54 + 1024 * 3 * 768:
    bad.append(f'the header: {b[:2]} {w}x{h} planes {planes} bits {bits} comp {comp}, {len(b)} bytes')
else:
    plate = lambda x, y: (lambda o: b[o + 2] << 16 | b[o + 1] << 8 | b[o])(off + (767 - y) * 3072 + x * 3)
    s1 = ppm('s1')
    # the pointer is on the screen and not in the plate: every difference must lie in one 11x17 arrow
    diff = [(x, y) for y in range(24, 768) for x in range(1024) if plate(x, y) != s1(x, y)]
    if diff:
        xs, ys = [d[0] for d in diff], [d[1] for d in diff]
        if max(xs) - min(xs) >= 11 or max(ys) - min(ys) >= 17: bad.append(f'{len(diff)} pixels differ from the screen, over {min(xs)}..{max(xs)} x {min(ys)}..{max(ys)}')
    pointer = (lambda x0, y0: lambda x, y: x0 <= x < x0 + 11 and y0 <= y < y0 + 17)(min(xs), min(ys)) if diff else (lambda x, y: False)
    s2 = ppm('s2')                                 # drawn back: row k of the plate at y 24 + k (taller than the space, from the top)
    wrong = sum(s2(x, y) != plate(x, y - 24) for y in range(560, 700, 5) for x in range(800, 1000, 4) if not pointer(x, y - 24))
    if wrong: bad.append(f'drawn back: {wrong} sampled pixels differ')
for x in bad: print('        ' + x)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] plate ' "$WORK/log" | sed -E 's/ in [0-9]+ ms$//' > "$WORK/events"
printf '%s\n' '[countertop] plate saved /plate1.bmp, 2359350 bytes' '[countertop] plate saved /plate2.bmp, 2359350 bytes' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
took=$(grep -oE 'plate saved /plate1\.bmp, 2359350 bytes in [0-9]+ ms' "$WORK/log" | grep -oE '[0-9]+ ms')
[ "$fail" = 0 ] && echo "  ok    a picture of the screen: Print Screen saves /plate1.bmp ($took), the screen's own pixels; then plate2.bmp; drawn back as the background" \
    || echo "  FAIL  a picture of the screen"
exit $fail

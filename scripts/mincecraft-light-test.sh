#!/usr/bin/env bash
# mincecraft-light-test.sh - tealights light the night (v0.60.216). Boot 1:
# the flat world at midnight, a screendump looking ahead, P saves it. On
# the host its grid gets a tealight at (64, 11, 68), on the crust 3.5 blocks
# ahead (the run-length grid decoded and made again, the CRC-32 made
# again). Boot 2 loads it, the same place and view: the crust around the
# tealight brighter than in boot 1, far crust as it was, the tealight itself
# lit; the frame rate still at its cap (33). Then crafting four from butter
# would make more; that recipe is mincecraft-craft-test's.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mclight.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1: name, $2: the arguments
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $2 -wait -time 0 -world /L.WLD &" "WAIT:1" "countertop" "WAIT:8"
                "MON:screendump $WORK/$1.ppm" "WAIT:1" "TYPE:p" "WAIT:2" "KEY:esc" "WAIT:3")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$1.log" -monitor "unix:$WORK/mon-$1.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$1.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($1)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot dark "-flat"
mcopy -n -i "$WORK/disk.img" ::L.WLD "$WORK/l.wld" 2>/dev/null || { echo "  FAIL  no /L.WLD saved"; fail=1; }
python3 -I - "$WORK/l.wld" <<'EDIT' || { echo "  FAIL  could not put the tealight in"; fail=1; }
import struct, sys, zlib
p = sys.argv[1]; b = open(p, 'rb').read()
assert b[:4] == b'MINC' and struct.unpack('<I', b[4:8])[0] == 3
head = b[:92]                                     # "MINC", eight words, fourteen counts
grid = bytearray()
i = 92
while len(grid) < 128 * 128 * 64:
    grid += bytes([b[i + 1]]) * b[i]; i += 2
x, y, z = 64, 11, 68
grid[(y * 128 + z) * 128 + x] = 13               # B_TEALIGHT
out = bytearray(head); i = 0
while i < len(grid):
    n = 1
    while i + n < len(grid) and n < 255 and grid[i + n] == grid[i]: n += 1
    out += bytes([n, grid[i]]); i += n
out += struct.pack('<I', zlib.crc32(bytes(out)))
open(p, 'wb').write(out)
EDIT
mcopy -o -i "$WORK/disk.img" "$WORK/l.wld" ::L.WLD
boot lit ""
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: px[((93+y)*W+241+x)*3:((93+y)*W+241+x)*3+3]
lum = lambda c: c[0] + c[1] + c[2]
dark, lit = ppm('dark'), ppm('lit')
log = open(f'{work}/serial-lit.log', 'rb').read().decode('latin-1')
check('[mincecraft] loaded /L.WLD' in log, 'the world with the tealight loaded')
near = [(x, y) for y in range(310, 420, 6) for x in list(range(150, 250, 6)) + list(range(395, 495, 6))]   # the crust either side of the tealight
far = [(x, y) for y in range(250, 270, 4) for x in list(range(0, 80, 6)) + list(range(560, 640, 6))]
d_near = sum(lum(dark(x, y)) for x, y in near) / len(near); l_near = sum(lum(lit(x, y)) for x, y in near) / len(near)
d_far = sum(lum(dark(x, y)) for x, y in far) / len(far); l_far = sum(lum(lit(x, y)) for x, y in far) / len(far)
check(l_near > d_near * 1.6, 'the crust about the tealight brighter: %.0f, %.0f without it' % (l_near, d_near))
check(abs(l_far - d_far) <= max(8, d_far * 0.1), 'far crust as it was: %.0f, %.0f without' % (l_far, d_far))
face = [lum(lit(x, y)) for x in range(280, 360, 4) for y in range(340, 420, 4)]          # its face, 256..385 x 300..430
check(sum(face) / len(face) > 450, 'the tealight itself lit: %.0f' % (sum(face) / len(face)))
fps = [int(v) for v in re.findall(r'\[mincecraft\] fps=(\d+)', log)]
check(fps and min(fps) >= 30, 'the frame rate at its cap with the light: %r' % fps)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-light-test: PASS" || echo "mincecraft-light-test: FAIL"
exit "$fail"

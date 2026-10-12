#!/usr/bin/env bash
# mincecraft-health-test.sh - hearts and falling (v0.60.221). Boot 1 saves
# the flat world. On the host three pillars go into its grid, each on the
# crust (top y 10): A at (70, 64), 9 high; B at (64, 71) beside the broth
# pool (z 72..79), 9 high; C at (80, 64), 14 high; and seven crust in hand.
# Each boot after starts on one pillar's top facing +z and walks off it:
#   A: a 9 block fall onto crust takes 6 hearts (one a block past three),
#      10 to 4, in the markers; the HUD's ten hearts show 4 red, 6 empty.
#   B: the same height into the broth: none taken.
#   C: 14 blocks takes all ten: the cook wakes at the spawn with 10
#      hearts and the seven crust still in hand.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mchealth.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1: name, $2: arguments, then the keys after the window
    local name=$1 args=$2; shift 2
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $args -script -wait -time 600 -world /H.WLD &" "WAIT:1" "countertop"
                "UNTIL:[mincecraft] window up" "WAIT:2" "$@" "KEY:esc" "WAIT:3")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$name.log" -monitor "unix:$WORK/mon-$name.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($name)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot made "-flat" "TYPE:p" "WAIT:2"
mcopy -n -i "$WORK/disk.img" ::H.WLD "$WORK/base.wld" 2>/dev/null || { echo "  FAIL  no /H.WLD saved"; fail=1; }
for who in a b c; do
python3 -I - "$WORK/base.wld" "$WORK/$who.wld" "$who" <<'EDIT' || { echo "  FAIL  could not make the pillar world ($who)"; fail=1; }
import struct, sys, zlib
src, dst, who = sys.argv[1:]
b = open(src, 'rb').read()
assert b[:4] == b'MINC' and struct.unpack('<I', b[4:8])[0] == 5, 'a version 5 save'
H = 100                                          # "MINC", eight words, fourteen counts, the hearts, flying (v0.60.249)
grid = bytearray(); i = H
while len(grid) < 128 * 128 * 64:
    grid += bytes([b[i + 1]]) * b[i]; i += 2
pillars = {'a': (70, 64, 9), 'b': (64, 71, 9), 'c': (80, 64, 14)}
for x, z, h in pillars.values():
    for y in range(11, 11 + h): grid[(y * 128 + z) * 128 + x] = 3      # B_PARMESAN
x, z, h = pillars[who]
head = bytearray(b[:H])
head[16:28] = struct.pack('<iii', (x << 16) + 32768, (11 + h) << 16, (z << 16) + 32768)   # on its top
head[28:36] = struct.pack('<ii', 0, 0)           # yaw 0 (+z), level
head[36 + 4 * 1:40 + 4 * 1] = struct.pack('<I', 7)                    # seven crust
head[92:96] = struct.pack('<I', 10)              # ten hearts
out = bytearray(head); i = 0
while i < len(grid):
    n = 1
    while i + n < len(grid) and n < 255 and grid[i + n] == grid[i]: n += 1
    out += bytes([n, grid[i]]); i += n
out += struct.pack('<I', zlib.crc32(bytes(out)))
open(dst, 'wb').write(out)
EDIT
done
STEP=("KEY:w 600" "WAIT:4")
mcopy -o -i "$WORK/disk.img" "$WORK/a.wld" ::H.WLD; boot a "" "${STEP[@]}" "MON:screendump $WORK/a.ppm" "WAIT:1"
mcopy -o -i "$WORK/disk.img" "$WORK/b.wld" ::H.WLD; boot b "" "${STEP[@]}"
mcopy -o -i "$WORK/disk.img" "$WORK/c.wld" ::H.WLD; boot c "" "${STEP[@]}"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def log(n): return open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
hurt = lambda t: [(int(a), int(b)) for a, b in re.findall(r'\[mincecraft\] hurt (\d+) hearts=(\d+)', t)]
a, b, c = log('a'), log('b'), log('c')
check(hurt(a) == [(6, 4)], 'a 9 block fall onto crust: 6 hearts taken, 4 left: %r' % hurt(a))
check(hurt(b) == [], 'the same fall into the broth: none taken: %r' % hurt(b))
check(hurt(c) == [(11, 0)] and '[mincecraft] woke at the spawn hearts=10' in c, 'a 14 block fall: all taken, woke at the spawn: %r' % hurt(c))
after = c.split('woke at the spawn', 1)[1] if 'woke at the spawn' in c else ''
ps = [tuple(int(v) for v in m.groups()) for m in re.finditer(r'\[mincecraft\] p (-?\d+) (-?\d+) (-?\d+)', after)]
check(ps and abs(ps[0][0] - (64 << 16)) < 3 << 16 and abs(ps[0][2] - (64 << 16)) < 3 << 16, 'at the spawn, near (64, 64): %r' % (ps[:1],))
inv = re.findall(r'\[mincecraft\] inv[^\n]*', c)
check(inv == ['[mincecraft] inv 1=7'], 'the seven crust still in hand (no inventory change after waking): %r' % inv)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3], px[(y*W+x)*3+1], px[(y*W+x)*3+2])
img = ppm('a')
BX, BY = 241, 93                                  # the window's body
cells = [img(BX + 122 + k * 16 + 7, BY + 416 + 5) for k in range(10)]
red = [r > 180 and g < 90 and bl < 110 for r, g, bl in cells]
check(red == [True] * 4 + [False] * 6, "the HUD's hearts: 4 red then 6 empty: %r" % cells)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-health-test: PASS" || echo "mincecraft-health-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# doom-sky-test.sh - the sky is SKY1 where Doom puts it (v0.60.299, review
# R-D20). sky_ensure decoded SKY1 with patch_blit, which takes the patch's
# offsets (127, 123) off, so all but a 129x5 strip fell outside the buffer
# and the sky was black; it also turned the wrong way, at a quarter of
# Doom's scale, and squeezed 128 rows into 84. Here the player stands in
# E1M1 at the courtyard windows (doom at 1180 -3232 0, facing east, then at
# 350 degrees, so the turn's direction counts too) and two
# boxes of sky between the pillars are compared with SKY1 by Doom's rule:
# the column is (viewangle + xtoviewangle[x]) >> ANGLETOSKYSHIFT, 1024 to a
# turn and 256 wide, so it wraps; the row is skytexturemid (100) + y -
# centery (84), wrapped at 128. A pixel matches if SKY1 one column either
# side has its colour (xtoviewangle is a table, rounding at the edges).
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomsky.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
for view in "0 sky0" "350 sky350"; do set -- $view
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 1180 -3232 $1" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:3" "MON:screendump $WORK/$2.ppm" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
done
python3 - "$WORK" <<'PY' || fail=1
import math, struct, sys
work = sys.argv[1]
wad = open('DOOM1.WAD', 'rb').read(); n, off = struct.unpack('<4xII', wad[:12])
lump = {}
for i in range(n):
    p, sz, nm = struct.unpack('<II8s', wad[off + 16 * i:off + 16 * i + 16]); lump.setdefault(nm.rstrip(b'\0').decode(), wad[p:p + sz])
pal = lump['PLAYPAL'][:768]
g = lump['SKY1']; w, h = struct.unpack('<hh', g[:4]); sky = {}
for c in range(w):                            # the texture's own columns: no offsets
    q = struct.unpack('<I', g[8 + 4 * c:12 + 4 * c])[0]
    while g[q] != 0xFF:
        top, cnt = g[q], g[q + 1]
        for k in range(cnt): sky[(c, top + k)] = g[q + 3 + k]
        q += cnt + 4
def near(a, b): return all(abs(p - q) <= 12 for p, q in zip(a, b))
allok = True
# east, and 350 degrees: the angle's 124 of 128 there (-11.25), and the
# windows have moved left, so the boxes go with them.
for name, turn, boxes in (('sky0', 0, ((65, 125), (200, 255))), ('sky350', 124 / 128, ((65, 90), (170, 220)))):
    d = open(f'{work}/{name}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    def at(x, y):                             # the middle of the 3x3 cell of 320x200 pixel (x, y)
        o = ((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3; return px[o:o + 3]
    good = tot = 0
    for x0, x1 in boxes:
        for y in range(5, 31, 3):
            for x in range(x0, x1, 2):
                ang = turn * 2 * math.pi + math.atan((160 - x - 0.5) / 160)
                u = int(math.floor(ang / (2 * math.pi) * 1024)); v = (100 + y - 84) & 127
                want = [pal[sky[((u + du) & 255, v)] * 3:sky[((u + du) & 255, v)] * 3 + 3] for du in (-1, 0, 1)]
                tot += 1; good += any(near(at(x, y), c) for c in want)
    ok = good * 10 >= tot * 9; allok &= ok
    print(('  ok    ' if ok else '  FAIL  ') + f'facing {name[3:]}: the sky through the courtyard windows is SKY1 by Doom\'s rule: {good} of {tot} pixels')
sys.exit(0 if allok else 1)
PY
[ "$fail" = 0 ] && echo "doom-sky-test: PASS" || echo "doom-sky-test: FAIL"
exit "$fail"

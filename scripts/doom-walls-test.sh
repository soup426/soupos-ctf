#!/usr/bin/env bash
# doom-walls-test.sh - wall textures a texel to a map unit, pegged as Doom
# pegs them (v0.60.307, review R-D21). col_fill_tex stretched each texture
# over its wall's height (v_step = th / wall_h), and the upper and lower
# pegging offsets had their signs the wrong way. Doom (r_segs.c): the row is
# texturemid + (y - centery) * distance / projection, texturemid the front
# ceiling less the eye, or for a pegged upper the back ceiling and the
# texture's height, a pegged lower the back floor, an unpegged one-sided mid
# the floor and the texture's height; and the sidedef's row offset.
# Here the player stands in E1M1 at the courtyard windows facing east (doom
# at 1180 -3232 0), 164 from the wall at x=1344: the pillar between them
# (line 27, STARTAN3, pegged to its ceiling) and the window's lower strip
# (line 26, STARTAN3, lower unpegged) both have texturemid 200 - 25 = 175.
# Each column is compared with STARTAN3's at Doom's column along the wall,
# at the best of the 32 light levels (the light is not what is tested), a
# texel either way.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomwalls.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 1180 -3232 0" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:3" "MON:screendump $WORK/walls.ppm" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import struct, sys
work = sys.argv[1]
wad = open('DOOM1.WAD', 'rb').read(); n, off = struct.unpack('<4xII', wad[:12])
lump = {}
for i in range(n):
    p, sz, nm = struct.unpack('<II8s', wad[off + 16 * i:off + 16 * i + 16]); lump.setdefault(nm.rstrip(b'\0').decode(), wad[p:p + sz])
pal = lump['PLAYPAL'][:768]; cmap = lump['COLORMAP']
pn = lump['PNAMES']; names = [pn[4 + 8 * k:12 + 8 * k].rstrip(b'\0').decode().upper() for k in range(struct.unpack('<I', pn[:4])[0])]
t1 = lump['TEXTURE1']; tex = None
for k in range(struct.unpack('<I', t1[:4])[0]):
    o = struct.unpack('<I', t1[4 + 4 * k:8 + 4 * k])[0]
    if t1[o:o + 8].rstrip(b'\0') != b'STARTAN3': continue
    tw, th = struct.unpack('<hh', t1[o + 12:o + 16]); npt = struct.unpack('<h', t1[o + 20:o + 22])[0]
    tex = [[0] * th for _ in range(tw)]
    for q in range(npt):
        ox, oy, pi = struct.unpack('<hhh', t1[o + 22 + 10 * q:o + 28 + 10 * q])
        g = lump[names[pi]]; pw = struct.unpack('<h', g[:2])[0]
        for c in range(pw):
            if not 0 <= ox + c < tw: continue
            pq = struct.unpack('<I', g[8 + 4 * c:12 + 4 * c])[0]
            while g[pq] != 0xFF:
                top, cnt = g[pq], g[pq + 1]
                for r in range(cnt):
                    if 0 <= oy + top + r < th: tex[ox + c][oy + top + r] = g[pq + 3 + r]
                pq += cnt + 4
d = open(f'{work}/walls.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
def at(x, y):
    o = ((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3; return px[o:o + 3]
def near(a, b): return all(abs(p - q) <= 12 for p, q in zip(a, b))
dist, eye, mid = 164.0, 25.0, 175.0          # the wall at x=1344; floor -16 + 41; ceiling 200 - eye
def check(name, xs, ys, v1y):
    good_cols = 0
    for x in xs:
        yw = -3232 + (160 - x - 0.5) * dist / 160                  # where this column's ray meets the wall
        u = int(v1y - yw)                                          # from the line's first vertex, going south
        best = 0
        for k in range(32):
            hit = 0
            for y in ys:
                v = mid + (y + 0.5 - 84) * dist / 160
                want = [pal[cmap[k * 256 + tex[(u + du) % tw][int(v + dv) % th]] * 3:][:3] for du in (-1, 0, 1) for dv in (-1, 0, 1)]
                hit += any(near(at(x, y), c) for c in want)
            best = max(best, hit)
        good_cols += best * 10 >= len(ys) * 9
    ok = good_cols * 10 >= len(xs) * 9
    print(('  ok    ' if ok else '  FAIL  ') + f'{name}: {good_cols} of {len(xs)} columns are STARTAN3 a texel to a map unit, as Doom pegs it')
    return ok
r1 = check('the pillar (line 27, pegged to its ceiling)', range(134, 186, 3), range(4, 120, 2), -3200)
r2 = check("the window's lower strip (line 26, lower unpegged)", range(220, 282, 3), range(103, 122), -3264)   # the barrel at (1312, -3264) covers up to 216
sys.exit(0 if r1 and r2 else 1)
PY
[ "$fail" = 0 ] && echo "doom-walls-test: PASS" || echo "doom-walls-test: FAIL"
exit "$fail"

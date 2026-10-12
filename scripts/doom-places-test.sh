#!/usr/bin/env bash
# doom-places-test.sh - Doom's pictures where Doom puts them (v0.60.229),
# each drawn here from DOOM1.WAD by Doom's own rule and compared with the
# screen (960x600 at 32,84: a 320x200 pixel is the middle of a 3x3 cell):
#   the status bar of E1M1's opening frame, all but the face: STBAR, the
#     numbers as STlib_drawNum draws them (every digit a cell as wide as
#     STTNUM0, no leading zeros: 50 ammo, 100 health, 0 armor) and the
#     percent signs at ST_HEALTHX and ST_ARMORX;
#   the menu's skull at x - 32 (SKULLXOFF) and y - 5 of the first item;
#   the episode menu with three lines (DOOM1.WAD has no M_EPI4): three
#     downs bring the skull round to the first again.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomplaces.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:2" "doom" "WAIT:6" "MON:screendump $WORK/main.ppm" "WAIT:1"
      "KEY:ret" "WAIT:1" "MON:screendump $WORK/ep.ppm" "WAIT:1"
      "KEY:down" "WAIT:1" "KEY:down" "WAIT:1" "KEY:down" "WAIT:1" "MON:screendump $WORK/ep3.ppm" "WAIT:1"
      "KEY:up" "WAIT:1" "KEY:up" "WAIT:1" "KEY:up" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:4"
      "MON:screendump $WORK/level.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import struct, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
wad = open('DOOM1.WAD', 'rb').read(); n, off = struct.unpack('<4xII', wad[:12])
lump = {}
for i in range(n):
    p, sz, nm = struct.unpack('<II8s', wad[off + 16 * i:off + 16 * i + 16]); lump[nm.rstrip(b'\0').decode()] = wad[p:p + sz]
pal = lump['PLAYPAL'][:768]
def patch(name, x, y, into):                 # V_DrawPatch: the offsets taken from (x, y)
    g = lump[name]; w, h, lo, to = struct.unpack('<hhhh', g[:8])
    for c in range(w):
        q = struct.unpack('<I', g[8 + 4 * c:12 + 4 * c])[0]
        while g[q] != 0xFF:
            top, cnt = g[q], g[q + 1]
            for k in range(cnt): into[(x - lo + c, y - to + top + k)] = g[q + 3 + k]
            q += cnt + 4
def screen(nm):
    d = open(f'{work}/{nm}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    return lambda x, y: px[((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3:((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3 + 3]
def near(a, b): return all(abs(p - q) <= 12 for p, q in zip(a, b))
def num(v, rx, y, into):                     # STlib_drawNum
    w = struct.unpack('<h', lump['STTNUM0'][:2])[0]; x = rx
    if v == 0: patch('STTNUM0', x - w, y, into); return
    while v: x -= w; patch('STTNUM%d' % (v % 10), x, y, into); v //= 10
want = {}
patch('STBAR', 0, 168, want)
num(50, 44, 171, want); num(100, 90, 171, want); num(0, 221, 171, want)
patch('STTPRCNT', 90, 171, want); patch('STTPRCNT', 221, 171, want)
lv = screen('level')
cells = [(x, y) for (x, y) in want if 0 <= x < 320 and 168 <= y < 200 and not (143 <= x < 172)]   # the face (STFST00 reaches x 171) left out
bad = [(x, y) for (x, y) in cells if not near(lv(x, y), pal[3 * want[(x, y)]:3 * want[(x, y)] + 3])]
check(not bad, 'the status bar as Doom draws it (no leading zeros, digits in STTNUM0 cells, %% at the field\'s end): %d of %d pixels differ%s'
      % (len(bad), len(cells), (', first at %r' % (bad[:3],)) if bad else ''))
def skull_at(sc, x, y):                      # either frame of the skull, at (x, y)
    best = 0
    for nm in ('M_SKULL1', 'M_SKULL2'):
        s = {}; patch(nm, x, y, s)
        best = max(best, sum(near(sc(px, py), pal[3 * v:3 * v + 3]) for (px, py), v in s.items()) * 100 // len(s))
    return best
m = screen('main')
check(skull_at(m, 97 - 32, 64 - 5) >= 95, "the main menu's skull at Doom's place (65, 59): %d%% of its pixels" % skull_at(m, 65, 59))
e = screen('ep')
check(skull_at(e, 48 - 32, 63 - 5) >= 95, "the episode menu's skull at (16, 58): %d%%" % skull_at(e, 16, 58))
e3 = screen('ep3')
check(skull_at(e3, 16, 58) >= 95, 'three episodes: three downs bring the skull back to the first: %d%%' % skull_at(e3, 16, 58))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-places-test: PASS" || echo "doom-places-test: FAIL"
exit "$fail"

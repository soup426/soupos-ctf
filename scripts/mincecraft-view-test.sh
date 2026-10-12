#!/usr/bin/env bash
# mincecraft-view-test.sh - Mincecraft's view (v0.60.193), from screendumps
# of its 640x480 body (the first program window: 241,93 on the screen).
# Boot 1, the flat world at noon: the top rows are the noon sky exactly
# (nothing above the eye), the bottom rows are crust (the ground three
# blocks ahead), and the fps marker is there, 5 or more (check.sh runs
# tests side by side; the 15 fps goal is measured idle, in the changelog).
# Also the HUD: the crosshair and the hotbar's nine slots.
# Boot 3 (v0.60.196), the flat world at midnight (-time 0 holds the clock):
# the top rows are the midnight sky exactly, the ground is the noon
# picture's at 77/256 (each channel), and the clock reads differently.
# Boot 2, seed 42: a real world is drawn (several block colours, not all
# sky), kept as view.ppm for a look. Esc ends each (bye).
# It starts at the console with -wait, in the background, before the
# desktop, so its markers reach the serial log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcview.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
boot() {   # $1: name, $2: the arguments
    cp disk.img "$WORK/disk-$1.img"
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $2 -wait &" "WAIT:1" "countertop" "WAIT:9"
                "MON:screendump $WORK/$1.ppm" "WAIT:1" "KEY:esc" "WAIT:2")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk-$1.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$1.log" -monitor "unix:$WORK/mon-$1.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$1.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($1)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot flat "-flat -time 600"
boot view "-seed 42 -time 600"
boot night "-flat -time 0"
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
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
BX, BY, SKY = 241, 93, 0x87BEEB
def body(at, x, y): return at(BX + x, BY + y)
for n in ('flat', 'view', 'night'):
    log = open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
    check('[mincecraft] window up' in log, f'{n}: the window opened')
    fps = [int(v) for v in re.findall(r'\[mincecraft\] fps=(\d+)', log)]
    check(fps and max(fps) >= 5 and max(fps) <= 35, f'{n}: fps markers {fps}, 5 to 35 (v0.60.202: held to the desktop\'s ~30 draws a second)')
    check('[mincecraft] bye' in log, f'{n}: Esc ended it')
f = ppm('flat')
top = [body(f, x, y) for y in range(4) for x in range(0, 640, 8)]
check(all(c == SKY for c in top), 'flat: the top rows are the noon sky, %06x' % SKY)
# The HUD (v0.60.195): the crosshair XORs what is under it; above the
# horizon that is the sky, so its upper stroke is the sky there ^ 0xFFFFFF.
# The hotbar: nine 40x40 slots 44 apart from x 122, y 432; the first is the
# chosen one (white frame), the rest grey, dark inside, empty at the start.
# (v0.60.199: the sky is a gradient, so each stroke pixel is checked against
# the same row's sky a little to the side.)
cross = [(body(f, x, y), body(f, 340, y)) for x in (319, 320) for y in range(232, 239)]
check(all(c == side ^ 0xFFFFFF for c, side in cross), 'flat: the crosshair inverts the sky under it: %s' % sorted({'%06x/%06x' % p for p in cross})[:3])
check(body(f, 122, 432) == 0xFFFFFF and body(f, 124, 434) == 0x202020, 'flat: the first hotbar slot, chosen (white frame, dark inside)')
check(all(body(f, 122 + 44 * k, 432) == 0x808080 for k in range(1, 9)), 'flat: the other eight slots, grey frames')
# The sky (v0.60.199) lightens toward the horizon: a row partway down is
# brighter than the top's exact sky colour.
lum = lambda c: (c >> 16) + (c >> 8 & 255) + (c & 255)
check(lum(body(f, 320, 200)) > lum(SKY) + 12, 'flat: the sky lighter toward the horizon: %06x vs %06x' % (body(f, 320, 200), SKY))
# The flat world's broth pool (x 60..68, z 72..80, two deep), dead ahead
# 7.5 to 15.5 blocks off: in the centre column between rows 292 and 330,
# not crust, warm (red well over blue), not the sky.
pool = [body(f, 320, y) for y in range(292, 331, 2)]
check(all(c not in (0xC8873A, 0x8A5A22) and (c >> 16) > (c & 255) + 40 for c in pool),
      'flat: the broth pool ahead, in broth colours: %s' % sorted({'%06x' % c for c in pool})[:5])
# blocks.c's colours, base and alt, as render.c shades each face at noon
BLOCKS = {'crust': (0xC8873A, 0x8A5A22), 'crumb': (0xEAD9B0, 0xC9B489), 'parmesan': (0xEFE8C8, 0xD4C99A),
          'cast iron': (0x2A2A2E, 0x18181B), 'sugar': (0xFAFAF5, 0xDCDCE6), 'stalk': (0xA9C77A, 0x86A65A),
          'floret': (0x2F6B2A, 0x1D4A1A), 'peppercorn': (0xEFE8C8, 0x1A1410), 'butter': (0xF6DC6B, 0xEBCB52)}
def shade(c, k): return ((c >> 16 & 255) * k >> 8) << 16 | ((c >> 8 & 255) * k >> 8) << 8 | ((c & 255) * k >> 8)
pal = {}
for name, cs in BLOCKS.items():
    for c in cs:
        for k in (256, 128, 205, 166): pal.setdefault(shade(c, k), name)
low = {body(f, x, 476) for x in list(range(0, 120, 4)) + list(range(520, 640, 4))}   # beside the hotbar (the pool is further on)
check(low <= {0xC8873A, 0x8A5A22}, 'flat: the bottom row is crust tops, exactly: %s' % sorted('%06x' % c for c in low)[:6])
v = ppm('view')
seen = {}
for y in range(0, 480, 4):
    for x in range(0, 640, 4):
        c = body(v, x, y)
        if c in pal: seen[pal[c]] = seen.get(pal[c], 0) + 1
check(len([n for n, k in seen.items() if k >= 20]) >= 3, 'seed 42: three or more blocks drawn in their own colours: %r' % seen)
n = ppm('night')
top = [body(n, x, y) for y in range(4) for x in range(0, 640, 8)]
check(sum(c == 0x0F142D for c in top) >= 0.97 * len(top), 'night: the top rows are the midnight sky (a star or two aside): %s' % sorted({'%06x' % c for c in top})[:4])
# Stars (v0.60.201): by night some of the upper sky is far brighter than
# the sky; at noon none is.
stars = lambda at, sky: sum(1 for y in range(30, 200) for x in range(0, 640) if lum(at(BX + x, BY + y)) > lum(sky) + 90)   # below the clock and the name
check(stars(n, 0x0F142D) >= 20, 'night: stars: %d bright pixels in the upper sky' % stars(n, 0x0F142D))
check(stars(f, SKY) == 0, 'noon: no stars')
def chans(c): return (c >> 16, c >> 8 & 255, c & 255)
dim = [(body(f, x, 400), body(n, x, 400)) for x in range(0, 640, 16)]
check(all(all(b <= a * 0.31 + 2 for a, b in zip(chans(d), chans(m))) for d, m in dim) and any(d != m for d, m in dim),
      'night: the ground at 77/256 of noon: %s' % ['%06x>%06x' % p for p in dim[:3]])
clock = lambda at: [body(at, x, y) for x in range(592, 632) for y in range(8, 24)]
check(clock(f) != clock(n), 'night: the clock reads differently')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-view-test: PASS" || echo "mincecraft-view-test: FAIL"
exit "$fail"

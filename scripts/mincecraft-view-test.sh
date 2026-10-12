#!/usr/bin/env bash
# mincecraft-view-test.sh - Mincecraft's view (v0.60.193), from screendumps
# of its 640x480 body (the first program window: 241,93 on the screen).
# Boot 1, the flat world at noon: the top rows are the noon sky exactly
# (nothing above the eye), the bottom rows are crust (the ground three
# blocks ahead), and the fps marker is there, 5 or more (check.sh runs
# tests side by side; the 15 fps goal is measured idle, in the changelog).
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
for n in ('flat', 'view'):
    log = open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
    check('[mincecraft] window up' in log, f'{n}: the window opened')
    fps = [int(v) for v in re.findall(r'\[mincecraft\] fps=(\d+)', log)]
    check(fps and max(fps) >= 5, f'{n}: fps markers {fps}, 5 or more')
    check('[mincecraft] bye' in log, f'{n}: Esc ended it')
f = ppm('flat')
top = [body(f, x, y) for y in range(4) for x in range(0, 640, 8)]
check(all(c == SKY for c in top), 'flat: the top rows are the noon sky, %06x' % SKY)
# blocks.c's colours, base and alt, as render.c shades each face at noon
BLOCKS = {'crust': (0xC8873A, 0x8A5A22), 'crumb': (0xEAD9B0, 0xC9B489), 'parmesan': (0xEFE8C8, 0xD4C99A),
          'cast iron': (0x2A2A2E, 0x18181B), 'sugar': (0xFAFAF5, 0xDCDCE6), 'stalk': (0xA9C77A, 0x86A65A),
          'floret': (0x2F6B2A, 0x1D4A1A), 'peppercorn': (0xEFE8C8, 0x1A1410), 'butter': (0xF6DC6B, 0xEBCB52)}
def shade(c, k): return ((c >> 16 & 255) * k >> 8) << 16 | ((c >> 8 & 255) * k >> 8) << 8 | ((c & 255) * k >> 8)
pal = {}
for name, cs in BLOCKS.items():
    for c in cs:
        for k in (256, 128, 205, 166): pal.setdefault(shade(c, k), name)
low = {body(f, x, 476) for x in range(0, 640, 4)}
check(low <= {0xC8873A, 0x8A5A22}, 'flat: the bottom row is crust tops, exactly: %s' % sorted('%06x' % c for c in low)[:6])
v = ppm('view')
seen = {}
for y in range(0, 480, 4):
    for x in range(0, 640, 4):
        c = body(v, x, y)
        if c in pal: seen[pal[c]] = seen.get(pal[c], 0) + 1
check(len([n for n, k in seen.items() if k >= 20]) >= 3, 'seed 42: three or more blocks drawn in their own colours: %r' % seen)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-view-test: PASS" || echo "mincecraft-view-test: FAIL"
exit "$fail"

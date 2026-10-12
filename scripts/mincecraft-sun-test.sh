#!/usr/bin/env bash
# mincecraft-sun-test.sh - a sun and a moon (v0.60.244). Boot 1 saves the
# flat world; on the host its cook is turned and tilted for each boot
# after (the header's yaw and pitch), the clock held by -time, the clouds
# off (-clear): 08:00 facing east, 30 degrees up (the sun's angle then);
# noon looking up; midnight looking up. Each dump: the sun's square (warm
# white, 0xFFF0A0) or the moon's (cool white, 0xD8E0F0) near the middle of the view, and not
# the other; the frame rate still at its cap.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcsun.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1 name, $2 args, then the keys after the window
    local name=$1 args=$2; shift 2
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $args -clear -script -wait -world /S.WLD &" "WAIT:1" "countertop"
                "UNTIL:[mincecraft] window up" "UNTIL:[mincecraft] fps=" "$@" "KEY:esc" "WAIT:3")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$name.log" -monitor "unix:$WORK/mon-$name.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($name)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
aim() {   # $1 yaw, $2 pitch: the saved world's cook turned so
    python3 -I - "$WORK/base.wld" "$WORK/s.wld" "$1" "$2" <<'PY'
import struct, sys, zlib
b = bytearray(open(sys.argv[1], 'rb').read())
assert b[:4] == b'MINC' and struct.unpack('<I', b[4:8])[0] == 5
b[28:36] = struct.pack('<ii', int(sys.argv[3]), int(sys.argv[4]))
b[-4:] = struct.pack('<I', zlib.crc32(bytes(b[:-4])))
open(sys.argv[2], 'wb').write(b)
PY
    mcopy -o -i "$WORK/disk.img" "$WORK/s.wld" ::S.WLD
}
boot made "-flat" "TYPE:p" "WAIT:2"
mcopy -n -i "$WORK/disk.img" ::S.WLD "$WORK/base.wld" 2>/dev/null || { echo "  FAIL  no /S.WLD saved"; fail=1; }
# yaw 256 faces +x (east); pitch in 1024ths of a turn, 85 about 30 degrees, 227 the most (80)
aim 256 85;  boot morning "-time 400" "WAIT:1" "MON:screendump $WORK/morning.ppm" "WAIT:1"
aim 0 227;   boot noon "-time 600" "WAIT:1" "MON:screendump $WORK/noon.ppm" "WAIT:1"
aim 0 227;   boot midnight "-time 0" "WAIT:1" "MON:screendump $WORK/midnight.ppm" "WAIT:1"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    return lambda x, y: tuple(px[((93+y)*W+241+x)*3:((93+y)*W+241+x)*3+3])
near_rgb = lambda c, w: all(abs(a - b) <= 12 for a, b in zip(c, w))
sun = lambda c: near_rgb(c, (0xFF, 0xF0, 0xA0))                           # render.c's SUN_COL
moon = lambda c: near_rgb(c, (0xD8, 0xE0, 0xF0))                          # and MOON_COL
def where(at, f):                          # the middle of the pixels f picks, in the view above the HUD
    pts = [(x, y) for y in range(0, 400, 2) for x in range(0, 640, 2) if f(at(x, y))]
    if not pts: return None, 0
    return (sum(p[0] for p in pts) // len(pts), sum(p[1] for p in pts) // len(pts)), len(pts)
for n, f, other, what in (('morning', sun, moon, 'the sun, low in the east'), ('noon', sun, moon, 'the sun, overhead'), ('midnight', moon, sun, 'the moon, overhead')):
    at = ppm(n)
    (c, k) = where(at, f); (_, k2) = where(at, other)
    near = c and abs(c[0] - 320) < 120 and abs(c[1] - 240) < 140
    check(near and k > 40 and k2 < 10, '%s: %s at %r (%d samples; the other %d)' % (n, what, c, k, k2))
for n in ('morning', 'noon', 'midnight'):
    fps = [int(v) for v in re.findall(r'\[mincecraft\] fps=(\d+)', open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1'))]
    check(fps and min(fps) >= 30, '%s: the frame rate at its cap: %r' % (n, fps))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-sun-test: PASS" || echo "mincecraft-sun-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# mincecraft-cloud-test.sh - clouds (v0.60.237). The flat world at noon, the
# view tilted up (the up arrow held): a screendump, four seconds, another.
# In both, cloud-coloured pixels (cream: bright, a little warm) cover part
# of the sky, and between them they have moved (the cloud pixels differ);
# the frame rate is still at its cap (33).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mccloud.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -time 600 -world /C.WLD &" "WAIT:1" "countertop"
      "UNTIL:[mincecraft] window up" "WAIT:2" "KEY:up 1200" "WAIT:3" "MON:screendump $WORK/a.ppm" "WAIT:4" "MON:screendump $WORK/b.ppm" "WAIT:1" "KEY:esc" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
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
    return lambda x, y: tuple(px[(y*W+x)*3:(y*W+x)*3+3])
def cloud(c): r, g, b = c; return r > 225 and g > 215 and b > 185 and r - b > 8
pts = [(241 + x, 93 + y) for y in range(20, 380, 4) for x in range(0, 640, 4)]   # the view above the hotbar
a, b = ppm('a'), ppm('b')
ma = [cloud(a(*p)) for p in pts]; mb = [cloud(b(*p)) for p in pts]
na, nb, moved = sum(ma), sum(mb), sum(x != y for x, y in zip(ma, mb))
check(na > len(pts) // 20 and nb > len(pts) // 20, 'clouds in the sky: %d and %d of %d samples' % (na, nb, len(pts)))
check(na < len(pts) * 3 // 4, 'and sky between them: %d of %d cloud' % (na, len(pts)))
check(moved > len(pts) // 100, 'they moved in four seconds: %d samples changed' % moved)
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
fps = [int(v) for v in re.findall(r'\[mincecraft\] fps=(\d+)', log)]
check(fps and min(fps[1:] or fps) >= 30, 'the frame rate at its cap: %r' % fps)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-cloud-test: PASS" || echo "mincecraft-cloud-test: FAIL"
exit "$fail"

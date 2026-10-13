#!/usr/bin/env bash
# doom-spritecut-test.sh - a sprite is cut only by walls in front of it
# (v0.60.310, review R-D45). v0.60.308 had solid walls close the sprite span
# as they were drawn, and a sprite is wider than its subsector: walls of
# subsectors the BSP reaches first can stand behind it, and cut it with a
# straight edge. Here the player stands in E1M1's nukage room (doom at 3216
# -3184 270) as the level comes up (before the world runs: the imps wake).
# The imp on the ledge (thing 8, at 3440,-3472) spans about columns 25-48;
# the wall behind it cut it at 37. Its brown (red over green by 15; the wall
# is dark green) must fill a tenth or more of its left part (28-35, drawn
# either way) and of its right part (39-46). The port's sprite placement is
# not Doom's to the texel yet (R-D4), so the colour is what is compared.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomspritecut.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 3216 -3184 270" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "UNTIL:[doom] E1M1 up" "MON:screendump $WORK/imp.ppm" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
d = open(f'{sys.argv[1]}/imp.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
def at(x, y):
    o = ((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3; return px[o:o + 3]
def imp(c): return c[0] > c[1] + 15                # the imp is brown; the wall behind it dark green
def part(xs):
    pts = [(x, y) for x in xs for y in range(4, 25)]
    return sum(imp(at(x, y)) for x, y in pts) * 100 // len(pts)
left, right = part(range(28, 36)), part(range(39, 47))
ok = left >= 10 and right >= 10
print(('  ok    ' if ok else '  FAIL  ') + f"the imp's brown: {left}% of its left part (drawn), {right}% of its right part (the wall behind it cut that off, R-D45)")
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-spritecut-test: PASS" || echo "doom-spritecut-test: FAIL"
exit "$fail"

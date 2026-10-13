#!/usr/bin/env bash
# doom-nearclip-test.sh - a wall running past the player has its true depth
# on screen (v0.60.321, reviews R-D8, R-D53). A wall clipped at the near
# plane got a far-off end on screen, clamped, and its depth and texture were
# interpolated over the clamped span with the unclamped ends' values: wrong
# depths (64 for a true 157), smeared walls, and since v0.60.310 (sprites
# hidden only by nearer walls) the sprites beside such a wall were hidden.
# Here the player stands in E1M1 (doom at 3216 -3808 270) as the level comes
# up: the shotgun guy (thing 100) beside wall 429, which runs past the
# player. Where the wall's wrong depth hid him (columns 177-191, rows
# 84-109), his red is a tenth or more of the pixels (before the fix none).
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomnearclip.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 3216 -3808 270" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "UNTIL:[doom] E1M1 up" "MON:screendump $WORK/guy.ppm" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
d = open(f'{sys.argv[1]}/guy.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
def at(x, y):
    o = ((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3; return px[o:o + 3]
pts = [(x, y) for x in range(177, 192) for y in range(84, 110)]
red = sum(1 for x, y in pts if (lambda c: c[0] > c[1] + 40 and c[0] > c[2] + 40)(at(x, y)))
ok = red * 10 >= len(pts)
print(('  ok    ' if ok else '  FAIL  ') + f"the shotgun guy beside the wall running past: {red} of {len(pts)} pixels his red (R-D53)")
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-nearclip-test: PASS" || echo "doom-nearclip-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# doom-tics-test.sh - the world runs at Doom's 35 tics a second, whatever the
# frame rate (v0.60.309, review R-D3). Doors, lifts, monsters and missiles
# stepped once a drawn frame: 50 a second standing still, more while a key
# was held, so monsters sped up as you moved and the automap stopped the
# world. Here the player stands in E1M1 (doom at 1180 -3232 0) for 8 s,
# then turns for 8 s (a left every 50 ms): from "[doom] world N steps at
# tick T" every 35 steps, both rates are 35 a second, give or take 4.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomtics.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill "$PID" 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys=("headchef" "rosemary" "WAIT:2" "doom at 1180 -3232 0" "WAIT:6" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:2"
      "MON:screendump $WORK/a.ppm" "WAIT:8" "MON:screendump $WORK/b.ppm")
for i in $(seq 160); do keys+=("KEY:left"); done
keys+=("MON:screendump $WORK/c.ppm" "WAIT:1")
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
# The phases by the serial log's own lines: screendumps leave no mark, so the
# lines are split at the first left (the turning raises the draw rate).
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
pts = [(int(a), int(b)) for a, b in re.findall(r'\[doom\] world (\d+) steps at tick (\d+)', open(sys.argv[1], errors='replace').read())]
if len(pts) < 6: print(f'  FAIL  only {len(pts)} world lines'); sys.exit(1)
t0 = pts[0][1]
def rate(lo, hi):                                  # steps a second over the timer ticks [lo, hi)
    seg = [p for p in pts if lo <= p[1] - t0 < hi]
    if len(seg) < 2: return None
    return (seg[-1][0] - seg[0][0]) * 100.0 / (seg[-1][1] - seg[0][1])
idle, turn = rate(100, 750), rate(950, 1500)
ok = all(r is not None and 31 <= r <= 39 for r in (idle, turn))
fmt = lambda r: 'none' if r is None else f'{r:.1f}'
print(('  ok    ' if ok else '  FAIL  ') + f'the world steps {fmt(idle)} a second standing still and {fmt(turn)} turning (Doom: 35)')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-tics-test: PASS" || echo "doom-tics-test: FAIL"
exit "$fail"

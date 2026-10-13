#!/usr/bin/env bash
# doom-tics-test.sh - the world runs at Doom's 35 tics a second, whatever the
# frame rate and the view (v0.60.309, review R-D3). Doors, lifts, monsters
# and missiles stepped once a drawn 3D frame: 50 a second standing still, 73
# turning, and the automap (no 3D frame) stopped the world. Here the player
# stands in E1M1 (doom at 1180 -3232 0) for 8 s, turns for about 8 s (a left
# every 50 ms), then opens the automap for 6 s; each phase is its own pass
# of the driver, and the serial log's length between them splits its lines
# (v0.60.314, review R-T44: fixed windows of ticks could measure a late turn
# as standing still). From "[doom] world N steps at tick T" every 35 steps,
# each phase's rate is 35 a second, give or take 4.
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
size() { wc -c < "$WORK/serial.log"; }
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 1180 -3232 0" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "UNTIL:[doom] E1M1 up" "WAIT:8" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
o1=$(size)
turn=(); for i in $(seq 160); do turn+=("KEY:left"); done
QEMU_KEYS_NOWAIT=1 python3 scripts/qemu_keys.py "$WORK/mon.sock" "${turn[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (turning)"; fail=1; }
o2=$(size)
QEMU_KEYS_NOWAIT=1 python3 scripts/qemu_keys.py "$WORK/mon.sock" "KEY:tab" "WAIT:6" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (automap)"; fail=1; }
o3=$(size)
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" "$o1" "$o2" "$o3" <<'PY' || fail=1
import re, sys
raw = open(sys.argv[1], 'rb').read(); cut = [0] + [int(a) for a in sys.argv[2:]]
def rate(lo, hi):                                  # steps a second over this phase's lines, less its first
    pts = [(int(a), int(b)) for a, b in re.findall(rb'\[doom\] world (\d+) steps at tick (\d+)', raw[lo:hi])][1:]
    if len(pts) < 3: return None
    return (pts[-1][0] - pts[0][0]) * 100.0 / (pts[-1][1] - pts[0][1])
r = [rate(cut[i], cut[i + 1]) for i in range(3)]
ok = all(x is not None and 31 <= x <= 39 for x in r)
fmt = lambda x: 'none' if x is None else f'{x:.1f}'
print(('  ok    ' if ok else '  FAIL  ') + f'the world steps {fmt(r[0])} a second standing still, {fmt(r[1])} turning and {fmt(r[2])} in the automap (Doom: 35)')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-tics-test: PASS" || echo "doom-tics-test: FAIL"
exit "$fail"

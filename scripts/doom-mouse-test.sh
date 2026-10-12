#!/usr/bin/env bash
# doom-mouse-test.sh - Doom turns with the mouse (v0.60.220). E1M1's opening
# frame; 200 counts of mouse to the right turn the view (most of the 3D view
# changes); 200 back to the left turn it back to the very frame it started
# from (a turn, not a step: nothing moved); the left button held a second
# fires as Ctrl does, a shot each 0.15 s (the log's ticks 15 to 20 apart,
# the ammo down one a shot), where the cooldown had counted loop passes and emptied the clip.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doommouse.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
R="MON:mouse_move 50 0"; L="MON:mouse_move -50 0"
keys=("headchef" "rosemary" "WAIT:2" "doom" "WAIT:6" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:4"
      "MON:screendump $WORK/a.ppm" "WAIT:1"
      "$R" "$R" "$R" "$R" "WAIT:2" "MON:screendump $WORK/b.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "WAIT:2" "MON:screendump $WORK/c.ppm" "WAIT:1"
      "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
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
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: px[(y*W+x)*3:(y*W+x)*3+3]
OX, OY = 32, 84
pts = [(OX + x, OY + y) for y in range(0, 168 * 3, 6) for x in range(0, 960, 6)]
a, b, c = ppm('a'), ppm('b'), ppm('c')
ab = sum(a(*p) != b(*p) for p in pts); ac = sum(a(*p) != c(*p) for p in pts)
check(ab > len(pts) // 2, 'the mouse to the right turned the view: %d of %d samples changed' % (ab, len(pts)))
check(ac == 0, 'and back to the left, the frame it started from: %d of %d samples differ' % (ac, len(pts)))
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
fires = re.findall(r'\[doom\] fire ammo=(\d+) tick=(\d+)', log)
n = [int(f[0]) for f in fires]; t = [int(f[1]) for f in fires]
gaps = [b - a for a, b in zip(t, t[1:])]
check(len(n) >= 4 and n == list(range(n[0], n[0] - len(n), -1)), 'the left button held fired, the ammo down one a shot: %r' % n)
check(gaps and all(15 <= g <= 20 for g in gaps), 'a shot each 0.15 s, as Ctrl: gaps of %r ticks' % gaps)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-mouse-test: PASS" || echo "doom-mouse-test: FAIL"
exit "$fail"

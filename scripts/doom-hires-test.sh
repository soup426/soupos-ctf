#!/usr/bin/env bash
# doom-hires-test.sh - Doom drawn at 960x600 on the framebuffer (v0.60.213).
# E1M1's opening frame (no input; the scene is deterministic): the log says
# 960x600; the 3D view (the top 168 x 3 rows, 32..992 across, 84 down) has
# many 3x3 cells that are not one colour, which the old 3x scaler could not
# make; the status bar, a 2D picture placed in 320x200 coordinates, is still
# drawn in exact 3x3 cells; and TAB shows the automap (most of the screen its
# background), which had frozen on the last 3D frame on every framebuffer
# boot: draw_automap never presented its frame.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomhires.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:2" "doom" "WAIT:6" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:4"
      "MON:screendump $WORK/level.ppm" "WAIT:1" "KEY:tab" "WAIT:2" "MON:screendump $WORK/map.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
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
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
check('[doom] drawing at 960x600' in log, 'Doom drew at 960x600')
OX, OY = 32, 84                                   # (1024 - 960) / 2, (768 - 600) / 2
lv = ppm('level')
def mixed(y0, y1):                                # the 3x3 cells not of one colour, of all
    n = m = 0
    for cy in range(y0, y1, 3):
        for cx in range(0, 960, 3):
            c = {lv(OX + cx + a, OY + cy + b) for a in range(3) for b in range(3)}
            n += 1; m += len(c) > 1
    return m, n
m, n = mixed(0, 168 * 3)
check(m > n // 10, 'the 3D view drawn finely: %d of %d 3x3 cells are not one colour' % (m, n))
m, n = mixed(168 * 3, 600)
check(m == 0, 'the status bar drawn in exact 3x3 cells (a 320x200 picture): %d mixed' % m)
mp = ppm('map')
same = sum(1 for y in range(OY, OY + 504, 8) for x in range(OX, OX + 960, 8) if mp(x, y) == lv(x, y))
total = len(range(OY, OY + 504, 8)) * len(range(OX, OX + 960, 8))
check(same < total // 4, 'TAB shows the automap, not the frozen 3D view: %d of %d samples unchanged' % (same, total))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-hires-test: PASS" || echo "doom-hires-test: FAIL"
exit "$fail"

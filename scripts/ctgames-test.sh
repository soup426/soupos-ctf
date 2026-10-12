#!/usr/bin/env bash
# ctgames-test.sh - Mincecraft and Doom on the desktop (v0.60.239): two more
# icons down the left, named under them. A double click on Mincecraft's
# opens its window with no terminal on show (a hidden one cooks it); Esc
# ends it. (Its own [mincecraft] lines go to that hidden terminal, so the
# desktop's lines are what is waited for.) A double click on Doom runs it from the desktop itself (Doom needs
# the machine to itself and the real screen, so not from a terminal); its
# menu's Quit Game brings the desktop back, the icons as they were. With
# Mincecraft open, Doom's icon is refused with a note: another task is alive.
# (v0.60.245) neosim's icon, the 8th, opens it with no file ([No Name]) and no
# terminal on show.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-ctgames.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
# The icons: x 20-60, the nth at y 40 + 76n (40 square); Mincecraft the 6th
# (n 5, its middle 40,460), Doom the 7th (n 6, 40,536).
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:3" "MON:screendump $WORK/desk.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 40 230" "MON:mouse_move 0 230" "WAIT:1" "$TWICE"
      "UNTIL:[countertop] open Mincecraft for pid" "WAIT:3" "MON:screendump $WORK/mc.ppm" "WAIT:1" "KEY:esc" "UNTIL:[countertop] program" "WAIT:3"
      "MON:mouse_move 0 76" "WAIT:1" "$TWICE" "UNTIL:[doom] drawing at" "WAIT:4" "MON:screendump $WORK/doom.ppm" "WAIT:1"
      "KEY:up" "WAIT:1" "KEY:ret" "UNTIL:[countertop] doom over" "WAIT:2" "MON:mouse_move 200 0" "WAIT:1" "MON:screendump $WORK/back.ppm" "WAIT:1" "MON:mouse_move -200 0" "WAIT:1"
      "MON:mouse_move 0 -76" "WAIT:1" "$TWICE" "WAIT:5"
      "MON:mouse_move 0 76" "WAIT:1" "$TWICE" "UNTIL:[countertop] doom refused" "WAIT:1" "MON:screendump $WORK/refused.ppm" "WAIT:1"
      "MON:mouse_move 0 76" "WAIT:1" "$TWICE" "UNTIL:[countertop] open neosim for pid" "WAIT:2" "MON:screendump $WORK/neosim.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (a step never came)"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import os, re, sys
work, fonth = sys.argv[1], sys.argv[2]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127): shape.setdefault(font[c][:14], chr(c))
def ppm(n):
    if not os.path.exists(f'{work}/{n}.ppm'): return None
    d = open(f'{work}/{n}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
def label(at, n, name):                       # the name under the nth icon, in TEXT_RGB
    x0, y = 20 + 20 - 4 * len(name), 40 + 76 * n + 44
    s = ''
    for c in range(len(name)):
        g = bytes(sum(1 << (7 - i) for i in range(8) if at(x0 + c * 8 + i, y + j) == 0xF0F0F0) for j in range(14))
        s += shape.get(g, '?')
    return s
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
desk = ppm('desk')
check(desk and label(desk, 5, 'mincecraft') == 'mincecraft' and label(desk, 6, 'doom') == 'doom',
      'two more icons, named: %r %r' % ((label(desk, 5, 'mincecraft'), label(desk, 6, 'doom')) if desk else (None, None)))
check(log.count('[countertop] kitchen: mincecraft') == 2 and log.count('[countertop] open Mincecraft for pid') == 2,
      "Mincecraft's icon opened it (twice, the second time for the refusal): %d" % log.count('[countertop] open Mincecraft for pid'))
check(not re.search(r'\[countertop\] open Terminal', log), 'no terminal on show for it')
check('[countertop] doom\n' in log and '[doom] drawing at' in log, "Doom's icon ran Doom")
d, b = ppm('desk'), ppm('back')
same = d and b and all(d(x, y) == b(x, y) for y in range(40, 40 + 76 * 7) for x in range(0, 110, 3))
check(same, 'Quit Game: the desktop again, its icons as they were')
check(re.search(r'\[countertop\] doom refused: \d+ tasks', log), 'with Mincecraft open, Doom refused (another task alive)')
check(desk and label(desk, 7, 'neosim') == 'neosim', "neosim's icon, named: %r" % (label(desk, 7, 'neosim') if desk else None))
check('[countertop] kitchen: neosim' in log and '[countertop] open neosim for pid' in log and not re.search(r'\[countertop\] open Terminal', log),
      "neosim's icon opened it, no terminal on show")
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "ctgames-test: PASS" || echo "ctgames-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# mincecraft-f3-test.sh - F3 (v0.60.242): the flat world at noon, a few
# steps forward, F3: the screen's top-left rows read back against the font
# (white with a black shadow): "Mincecraft  N fps", "XYZ: x / y / z" to a
# tenth as the last [mincecraft] p marker has them, "Facing: south (+z)
# yaw 0", the block aimed at and its place as a break would say, the time.
# F3 again: gone.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcf3.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -clear -script -wait -time 600 -world /F3.WLD &" "WAIT:1" "countertop"
      "UNTIL:[mincecraft] window up" "UNTIL:[mincecraft] fps=" "KEY:w 400" "KEY:down 400" "WAIT:2" "KEY:f3" "WAIT:2" "MON:screendump $WORK/on.ppm" "WAIT:1"
      "TYPE:m" "WAIT:1" "KEY:f3" "WAIT:2" "MON:screendump $WORK/off.ppm" "WAIT:1" "KEY:esc" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import re, sys
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
    d = open(f'{work}/{n}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
def row(at, y):                               # white text from x 8
    s = ''
    for c in range(60):
        g = bytes(sum(1 << (7 - i) for i in range(8) if at(241 + 8 + c*8 + i, 93 + y + j) == 0xFFFFFF) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
before = log.split('[mincecraft] mark')[0]
p = [tuple(int(v) for v in m.groups()) for m in re.finditer(r'\[mincecraft\] p (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)', before)]
on = ppm('on')
rows = [row(on, 28 + 18 * i) for i in range(5)]
print('  rows:', rows)
tenth = lambda v: '%d.%d' % (v >> 16, ((v & 0xFFFF) * 10) >> 16)
if p:
    x, y, z, yaw, pitch = p[-1]
    check(rows[1] == 'XYZ: %s / %s / %s' % (tenth(x), tenth(y), tenth(z)), 'the place, as the marker: %r' % rows[1])
    check(rows[2] == 'Facing: south (+z)  yaw %d' % yaw, 'facing south, yaw %d: %r' % (yaw, rows[2]))
else:
    check(False, 'no position markers')
check(re.match(r'Mincecraft  \d+ fps$', rows[0]), 'the name and the frame rate: %r' % rows[0])
check(re.match(r'Block: crust at \d+ 10 \d+$', rows[3]), 'the block aimed at, down at the crust: %r' % rows[3])
check(rows[4] == 'Time: 12:00', 'the time: %r' % rows[4])
off = ppm('off')
check(all(row(off, 28 + 18 * i) == '' for i in range(5)), 'F3 again hides it')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-f3-test: PASS" || echo "mincecraft-f3-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# ctwheel-test.sh - the wheel (v0.60.151). The mouse answers the
# IntelliMouse knock with ID 3; a notch down over a files window scrolls
# it three rows, a notch up brings it back, another up at the top does
# nothing, a notch over the background does nothing, and thirty notches
# down stop with the last entry on the last row. (QEMU's mouse_move dz
# is one notch a command; a negative dz is the wheel turned down.)
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctwheel.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"; DN="MONQ:mouse_move 0 0 -1"; UP="MONQ:mouse_move 0 0 1"
MANY=(); for i in $(seq 30); do MANY+=("$DN"); done
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 350 12" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1"
      "MON:mouse_move 350 288" "WAIT:1" "$DN" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "$UP" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1" "$UP" "WAIT:1"
      "MON:mouse_move 0 300" "WAIT:1" "$DN" "WAIT:1" "MON:mouse_move 0 -300" "WAIT:1"
      "${MANY[@]}" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1" "KEY:esc" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
last=$(mdir -b -i "$WORK/disk.img" ::/ | sed 's|^::/||' | grep -v '/$' | tail -1)
python3 - "$WORK" src/font8x16.h "$last" <<'PY' || fail=1
import sys, re
work, fonth, last = sys.argv[1], sys.argv[2], sys.argv[3]
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127):
    shape.setdefault(font[c][:14], chr(c))
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
INK = {0x202020, 0x1E4E8C, 0xF0F0F0}
def row(at, i):                                   # the files window's row i: text at 615,112+18i
    s = ''
    for c in range(30):
        g = bytes(sum(1 << (7 - b) for b in range(8) if at(615 + c*8 + b, 112 + 18*i + j) in INK) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
bad = []
def want(got, exp, what):
    if got != exp: bad.append(f'{what}: "{got}", not "{exp}"')
want(row(ppm('s1'), 0), 'RECIPE.TXT', 'a notch down: row 0')
want(row(ppm('s2'), 0), 'etc/', 'a notch up: row 0')
want(row(ppm('s3'), 21), last, 'thirty down: the last row')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x '\[mouse\] id 3, a wheel' "$WORK/log" >/dev/null || { echo "        no wheel: $(grep '\[mouse\] id' "$WORK/log")"; fail=1; }
grep -o 'scrolled to row [0-9]*' "$WORK/log" | cut -d' ' -f4 | tr '\n' ' ' > "$WORK/rows"
python3 - "$WORK/rows" <<'PY' || fail=1
import sys
r = [int(x) for x in open(sys.argv[1]).read().split()]
# 3 down, back to 0, nothing more at the top or off the window, then 3, 6, ... to the end
ok = len(r) > 3 and r[:2] == [3, 0] and r[2:] == list(range(3, r[-1] + 1, 3))[:len(r) - 3] + [r[-1]] and r[-1] > 3
if not ok: print('        the scrolls: ' + ' '.join(map(str, r)))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "  ok    the wheel: ID 3 from the knock; a files window scrolls three rows a notch, both ways, stops at the top and with the last entry on the last row; not off the window" \
    || echo "  FAIL  the wheel"
exit $fail

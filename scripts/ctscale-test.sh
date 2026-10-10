#!/usr/bin/env bash
# ctscale-test.sh - scale.elf, the calculator (v0.60.173). Typed: 12.5x4= is
# 50, 7/2= is 3.5, 1/0= is an error, 2+3x4= is 20 (left to right). Clicked:
# C 9 x 9 = is 81. The display read back against the font, drawn twice its
# size.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctscale.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1")
# scale's body at 241,93: button (row r, column c) at 241+4+59c+27, 93+56+48r+22; C across row 4
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook scale.elf" "WAIT:2"
      "TYPE:12.5x4=" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "TYPE:c7/2=" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:c1/0=" "WAIT:1" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "TYPE:c2+3x4=" "WAIT:1" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move 161 351" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 29 -192" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 59 48" "WAIT:1" "${CLICK[@]}" "MON:mouse_move -59 -48" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 0 144" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s5.ppm" "WAIT:1"
      "TYPE:q" "WAIT:2" "MON:mouse_move -102 -303" "WAIT:1" "${CLICK[@]}" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re
work, fonth = sys.argv[1], sys.argv[2]
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
def display(n):                                   # 14 cells of 16x32 from 249,103, the font doubled
    at = ppm(n)
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(249 + c*16 + 2*b, 103 + 2*j) in (0xF0F0F0, 0xF05050)) for j in range(14)), '?')
                   for c in range(14)).strip()
want = {'s1': '50', 's2': '3.5', 's3': 'error', 's4': '20', 's5': '81'}
bad = [f'{n}: the display says "{display(n)}", not "{w}"' for n, w in want.items() if display(n) != w]
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
grep -x '\[countertop\] program closed scale' <(tr -d '\r' < "$WORK/serial.log") >/dev/null || { echo "        q did not close scale"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    scale.elf: 12.5x4=50, 7/2=3.5, 1/0 an error, 2+3x4=20 typed; C 9 x 9 = 81 clicked; read off its display" \
    || echo "  FAIL  scale.elf"
exit $fail

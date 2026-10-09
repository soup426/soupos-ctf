#!/usr/bin/env bash
# ctkitchen-test.sh - the Kitchen menu (v0.60.158). Its button drops down
# the programs that open windows; picking swirl opens a terminal that
# cooks it (swirl's window appears; q ends it); opened again, a click
# away closes it and the screen under it comes back. From screendumps
# and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctkitchen.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Kitchen's button 392-464 on the bar; its menu 392,24, 240 wide, rows of 24
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 420 12" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 0 50" "WAIT:1" "${CLICK[@]}" "WAIT:4" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:q" "WAIT:3"
      "MON:mouse_move 0 -50" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 400 500" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move -532 -550" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def word(at, ox, oy, n):
    s = ''
    for c in range(n):
        g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + j) == 0x202020) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
MENU = 0xF4F1EA
bad = []
s1 = ppm('s1')
if s1(600, 40) != MENU: bad.append(f's1: no menu at 600,40 ({s1(600,40):06x})')
for i, name in enumerate(('frost', 'swirl', 'oven', 'expo')):
    got = word(s1, 402, 24 + 2 + i * 24 + 4, 5)
    if got != name: bad.append(f's1: menu row {i} reads "{got}", not "{name}"')
s2 = ppm('s2')
if s2(600, 40) == MENU: bad.append('s2: the menu is still down after picking swirl')
if s2(600, 40) == MENU or s2(500, 200) in (MENU, 0x000000): bad.append(f's2: no swirl window at 500,200 ({s2(500,200):06x})')
s3 = ppm('s3')
if s3(600, 40) == MENU: bad.append('s3: the menu is still down after a click away')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (kitchen|open Terminal|open swirl|program closed swirl)' "$WORK/log" | sed 's/ for pid [0-9]* at .*//' > "$WORK/events"
printf '[countertop] kitchen menu open\n[countertop] kitchen menu closed\n[countertop] open Terminal 1\n[countertop] kitchen: swirl\n[countertop] open swirl\n[countertop] program closed swirl\n[countertop] kitchen menu open\n[countertop] kitchen menu closed\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the Kitchen menu: frost, swirl, oven and expo listed, swirl picked opens a terminal cooking it, a click away closes the menu" \
    || echo "  FAIL  the Kitchen menu"
exit $fail

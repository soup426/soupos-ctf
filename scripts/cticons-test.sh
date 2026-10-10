#!/usr/bin/env bash
# cticons-test.sh - icons on the background (v0.60.174). The Kitchen menu's
# programs down the left, a square each with its initial and its name; one
# click on frost's does nothing, nor does a click between icons; a double
# click on swirl's opens it as the menu does (a terminal cooking it, its
# window). Screendumps (the squares' colours, the initials against the
# font doubled) and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cticons.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
# icons at 20, 40 + 76i, 40 square: frost 0, swirl 1
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 40 60" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 0 48" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 0 28" "WAIT:1" "$TWICE" "WAIT:4" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:q" "WAIT:2" "MON:mouse_move 248 -124" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
s1 = ppm('s1')
COL = [0xC0392B, 0x2E86DE, 0xE67E22, 0x27AE60, 0x8E44AD]
bad = []
for i, (name, col) in enumerate(zip(('frost', 'swirl', 'oven', 'expo', 'scale'), COL)):
    y = 40 + 76 * i
    if s1(22, y + 2) != col: bad.append(f'{name}: {s1(22, y + 2):06x} at its corner, not {col:06x}')
    g = bytes(sum(1 << (7 - b) for b in range(8) if s1(32 + 2*b, y + 4 + 2*j) == 0xF0F0F0) for j in range(14))
    if shape.get(g) != name[0].upper(): bad.append(f'{name}: its initial reads "{shape.get(g)}"')
s2 = ppm('s2')
if s2(500, 200) in (0x000000,) or s2(242 + 5, 94 + 5) == s1(242 + 5, 94 + 5): bad.append("s2: no swirl window over the desktop")
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (kitchen|open swirl|program closed swirl)' "$WORK/log" | sed 's/ for pid.*//' > "$WORK/events"
printf '[countertop] kitchen: swirl\n[countertop] open swirl\n[countertop] program closed swirl\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    icons: frost, swirl, oven, expo, scale down the left; one click and a click between do nothing; a double click opens swirl" \
    || echo "  FAIL  icons on the background"
exit $fail

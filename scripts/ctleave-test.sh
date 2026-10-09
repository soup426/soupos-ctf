#!/usr/bin/env bash
# ctleave-test.sh - Leave asks first when programs have windows (v0.60.165).
# With frost open, one click on Leave keeps the desktop up and says so on
# the bar; five seconds later the question lapses and the note goes; two
# clicks in a row leave.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctleave.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1")
QUICK=$'MON:mouse_button 1\nmouse_button 0'                 # a click in one write: the 5 s are not spent waiting
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook frost.elf" "WAIT:2"
      "MON:mouse_move 88 0" "WAIT:1" "$QUICK" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:6" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "$QUICK" "WAIT:1" "$QUICK" "WAIT:3")
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
def note(at):                                     # the bar's note: amber text from 480, 4
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(480 + c*8 + b, 4 + j) == 0xF0B040) for j in range(14)), '?')
                   for c in range(45)).rstrip()
bad = []
n1 = note(ppm('s1'))
if n1 != 'a program is open: Leave again to end it': bad.append(f's1: the bar says "{n1}"')
n2 = note(ppm('s2'))
if n2: bad.append(f's2: the note did not lapse: "{n2}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (leave|down)' "$WORK/log" > "$WORK/events"
printf '[countertop] leave: 1 program window open, asking again\n[countertop] leave: 1 program window open, asking again\n[countertop] down\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    Leave with frost open asks first (the bar says so), the question lapses after 5 s, two clicks leave" \
    || echo "  FAIL  Leave asking first"
exit $fail

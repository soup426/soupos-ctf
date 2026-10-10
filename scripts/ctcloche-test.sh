#!/usr/bin/env bash
# ctcloche-test.sh - the cloche, locking the desktop (v0.60.176). From the
# Kitchen menu it covers the screen; Esc does not leave; a wrong secret is
# refused (said in red, logged); the headchef's secret lifts it. With
# "idle 3" in /countertop, three idle seconds bring it down; three wrong
# secrets make it wait, the right one typed in the wait is not heard, and
# after the wait it is. Screendumps (read against the font) and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcloche.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# the Kitchen menu at 420,12; its seventh row (cloche) at y 182; Leave at 288,12
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 420 12" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 0 170" "WAIT:1" "${CLICK[@]}"
      "MON:screendump $WORK/s1.ppm" "WAIT:1" "KEY:esc" "WAIT:1" "basil" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "rosemary" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move -132 -170" "WAIT:1" "${CLICK[@]}" "WAIT:1"
      "stir /countertop idle 3" "WAIT:1" "countertop" "WAIT:6" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "one" "WAIT:1" "two" "WAIT:1" "three" "WAIT:1" "rosemary" "WAIT:5" "rosemary" "WAIT:2"
      "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:3")
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
def word(at, x, y, n, ink):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(x + c*8 + b, y + j) == ink) for j in range(14)), '?')
                   for c in range(n)).rstrip()
LOCK = 0x101418
bad = []
s1 = ppm('s1')
if s1(600, 5) != LOCK or s1(1000, 700) != LOCK: bad.append('s1: the screen is not covered')
if word(s1, 424, 324, 16, 0xF0F0F0) != 'under the cloche': bad.append(f's1: "{word(s1, 424, 324, 16, 0xF0F0F0)}"')
if word(s1, 512 - 32, 354, 8, 0xF0B040) != 'headchef': bad.append('s1: no headchef on the lock')
msg = word(ppm('s2'), 512 - 88, 414, 22, 0xF05050)
if msg != 'that is not the secret': bad.append(f's2: the lock says "{msg}"')
s3 = ppm('s3')
if s3(600, 5) == LOCK or s3(300, 140) == LOCK: bad.append('s3: still under the cloche after the secret')
if ppm('s4')(1000, 700) != LOCK: bad.append('s4: three idle seconds did not bring it down')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (cloche|settings: the cloche|down)' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] cloche down (the menu)' '[countertop] cloche: a wrong secret (1)' '[countertop] cloche lifted' '[countertop] down' \
       '[countertop] settings: the cloche after 3 s idle' '[countertop] cloche down (idle)' \
       '[countertop] cloche: a wrong secret (1)' '[countertop] cloche: a wrong secret (2)' '[countertop] cloche: a wrong secret (3)' \
       '[countertop] cloche lifted' '[countertop] down' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the cloche: from the menu, Esc held, a wrong secret refused, the right one lifts it; idle 3 s brings it down; three wrong make it wait, the secret in the wait unheard" \
    || echo "  FAIL  the cloche"
exit $fail

#!/usr/bin/env bash
# ctclockin-test.sh - straight to the desktop (v0.60.179). Booted with
# QEMU's -fw_cfg opt/soupos/desktop (as `make run` does), the console
# clocks in on the desktop's own screen: it is read back; a wrong secret
# is refused there; the headchef's opens their desktop; Leave clocks out
# back to the clock-in screen, not the prompt.
# Screendumps read against the font, and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctclockin.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Leave on the bar at 260-332
keys=("WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "headchef" "WAIT:1" "basil" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "headchef" "WAIT:1" "rosemary" "WAIT:3" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 288 12" "WAIT:1" "${CLICK[@]}" "WAIT:3" "MON:screendump $WORK/s4.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -fw_cfg name=opt/soupos/desktop,string=1 \
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
def clock_in_screen(n):
    s = ppm(n)
    if s(600, 5) != LOCK or s(1000, 700) != LOCK: bad.append(f'{n}: the screen is not covered')
    t = word(s, 400, 324, 28, 0xF0F0F0)
    if t != 'clock in to start your shift': bad.append(f'{n}: "{t}"')
    k = word(s, 456, 294, 14, 0xF0B040)
    if k != 'soupOS kitchen': bad.append(f'{n}: "{k}"')
    return s
s1 = clock_in_screen('s1')
c = word(s1, 432, 364, 9, 0xF0F0F0)
if c != 'cook:   _': bad.append(f's1: the cook field reads "{c}"')
s2 = clock_in_screen('s2')
m = word(s2, 432, 424, 20, 0xF05050)
if m != 'wrong cook or secret': bad.append(f's2: "{m}"')
s3 = ppm('s3')
if s3(600, 5) == LOCK or s3(1000, 700) == LOCK: bad.append('s3: still the clock-in screen after the right secret')
if word(s3, 10, 4, 6, 0xF0F0F0) != 'soupOS': bad.append(f's3: the bar reads "{word(s3, 10, 4, 6, 0xF0F0F0)}"')
clock_in_screen('s4')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (cloche|clock|down)' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] cloche down (clock in)' '[countertop] clock in to start your shift' '[countertop] clock-in refused (1)' \
       '[countertop] clocked in headchef' '[countertop] down' \
       '[countertop] clocked out' '[countertop] cloche down (clock in)' '[countertop] clock in to start your shift' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -q 'Welcome to the kitchen' "$WORK/log" && { echo "        the console's prompt clocked someone in"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    straight to the desktop: its clock-in screen at boot, a wrong secret refused there, the headchef's desktop, Leave back to the clock-in screen" \
    || echo "  FAIL  straight to the desktop"
exit $fail

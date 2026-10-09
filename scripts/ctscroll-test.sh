#!/usr/bin/env bash
# ctscroll-test.sh - scrollback in terminal windows (v0.60.166). tally.elf
# 60 fills Terminal 1 past its 25 rows; three notches of the wheel up show
# the same lines nine rows lower, with the lines before them above (the
# number before the old top one); twenty more reach the very first lines
# (the welcome); a key typed puts it back at the bottom, the key on the
# prompt. Cells read back against the font.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctscroll.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
UP="MONQ:mouse_move 0 0 1"                        # one notch up (QEMU's +1 is the wheel turned up)
# the pointer rests on Terminal 1's title bar (300,62): its arrow clear of the cells
MANY=(); for i in $(seq 20); do MANY+=("$UP"); done
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "cook tally.elf 60" "WAIT:3" "MON:mouse_move 100 50" "WAIT:1" "MON:screendump $WORK/s0.ppm" "WAIT:1"
      "$UP" "$UP" "$UP" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "${MANY[@]}" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:x" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "KEY:backspace" "WAIT:1" "MON:mouse_move -12 -50" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def term(n):
    at = ppm(n)
    # the cursor is an underline in a cell's last two rows: read the top 14
    return [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(61 + c*8 + b, 83 + r*16 + j) != 0) for j in range(14)), '?')
                    for c in range(80)).rstrip() for r in range(25)]
s0, s1, s2, s3 = term('s0'), term('s1'), term('s2'), term('s3')
bad = []
if s1[9:25] != s0[0:16]: bad.append('three notches up: the live rows are not nine lower: ' + ' | '.join(s1[:12]))
nums = [r for r in s0 if r.isdigit()]
if not nums or not s0[0].isdigit(): bad.append('s0 does not start with a number: ' + ' | '.join(s0[:3]))
elif s1[8] != str(int(s0[0]) - 1): bad.append(f'the line above the old top is "{s1[8]}", not {int(s0[0]) - 1}')
if not any('A terminal on the countertop' in r for r in s2): bad.append('all the way up: no welcome: ' + ' | '.join(s2[:4]))
if s3[:24] != s0[1:25] and s3[:24] != s0[:24]: bad.append('typing did not bring the bottom back: ' + ' | '.join(s3[-3:]))
if not s3[-1].endswith('> x'): bad.append(f'the key is not on the prompt: "{s3[-1]}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] Terminal 1 scrolled back' "$WORK/log" | head -3 > "$WORK/events"
printf '[countertop] Terminal 1 scrolled back 3\n[countertop] Terminal 1 scrolled back 6\n[countertop] Terminal 1 scrolled back 9\n' | cmp -s - "$WORK/events" \
    || { echo "        the scrolls: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    scrollback: the wheel moves a terminal back through what scrolled off (nine rows, then to the welcome), a key brings it to the bottom" \
    || echo "  FAIL  scrollback in terminal windows"
exit $fail

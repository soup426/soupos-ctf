#!/usr/bin/env bash
# ctoven-test.sh - oven.elf, the kitchen timer in a window (v0.60.164), with
# a sound card. Three seconds: counting (the window dark, its colon lit),
# then done (red, 00:00 in white, the middle segments dark, the mixer's
# 880 Hz beep in the log); q ends it. Sixty seconds stopped early with q:
# the terminal says where it stopped.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctoven.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "cook oven.elf 3" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:5" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:q" "WAIT:2" "cook oven.elf 60" "WAIT:2" "TYPE:q" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 88 0" "WAIT:1" "${CLICK[@]}" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none -audiodev none,id=a0 -device AC97,audiodev=a0 \
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
BX, BY = 241, 93                                  # oven's body
DARK, AMBER, RED, WHITE = 0x202428, 0xF0B040, 0xB03028, 0xFFFFFF
bad = []
def want(at, x, y, c, what):
    if at(BX + x, BY + y) != c: bad.append(f'{what}: {at(BX + x, BY + y):06x} at body {x},{y}, not {c:06x}')
s1 = ppm('s1')
want(s1, 4, 4, DARK, 's1 counting: dark'); want(s1, 114, 40, AMBER, 's1 counting: the colon lit')
s2 = ppm('s2')
want(s2, 4, 4, RED, 's2 done: red'); want(s2, 114, 40, WHITE, 's2 done: the colon white')
for x in (28, 68, 130, 170):                      # each digit a 0: its top lit, its middle dark
    want(s2, x + 15, 22, WHITE, f's2 done: the 0 at {x} has its top'); want(s2, x + 15, 50, RED, f's2 done: the 0 at {x} has no middle')
s3 = ppm('s3')
t = [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if s3(61 + c*8 + b, 83 + r*16 + j) != 0) for j in range(14)), '?')
             for c in range(80)).rstrip() for r in range(25)]
if 'oven: done' not in t: bad.append('Terminal 1: no "oven: done"')
if not any(re.fullmatch(r'oven: stopped at 00:5[0-9]', r) for r in t): bad.append('Terminal 1: no "oven: stopped at 00:5x": ' + ' | '.join(r for r in t if r)[:300])
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x '\[mixer\] tone 880 Hz for 600 ms' "$WORK/log" >/dev/null || { echo "        no beep: $(grep '\[mixer\]' "$WORK/log" | tr '\n' ';')"; fail=1; }
[ "$(grep -c '^\[countertop\] program closed oven' "$WORK/log")" = 2 ] || { echo "        oven did not close its window twice"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    oven.elf: counts down, goes red at 00:00 and beeps 880 Hz, q ends it; stopped early it says where" \
    || echo "  FAIL  oven.elf"
exit $fail

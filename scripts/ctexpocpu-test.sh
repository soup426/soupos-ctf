#!/usr/bin/env bash
# ctexpocpu-test.sh - CPU use in expo (v0.60.180). expo, then glutton.elf,
# from a desktop terminal: by pid expo is first and glutton second, glutton
# near 100% (it never yields) and expo near 0 (it waits for events); c sorts
# by CPU, glutton first, the chosen row going with its process; c again is
# by pid. Read off expo's window against the font.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctexpocpu.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Terminal 1 at 60,60 (to ~702,~500), expo at 240,70 (to 662,394): 100,450 is the terminal's alone,
# and expo is under it all, so it comes up by its taskbar button (the fourth, 584-734)
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook expo.elf &" "WAIT:3"
      "MON:mouse_move -100 438" "WAIT:1" "${CLICK[@]}" "cook glutton.elf &" "WAIT:4"
      "MON:mouse_move 550 304" "WAIT:1" "${CLICK[@]}" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "TYPE:c" "WAIT:3" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "TYPE:c" "WAIT:3" "MON:screendump $WORK/s3.ppm" "WAIT:1" "TYPE:q" "WAIT:2"
      "MON:mouse_move -362 -742" "WAIT:1" "${CLICK[@]}" "WAIT:3")
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
BX, BY = 241, 93                                  # expo's body
INK = {0x202020, 0xF0F0F0}
def line(at, y, n=50):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(BX + 8 + c*8 + b, BY + y + j) in INK) for j in range(14)), '?')
                   for c in range(n)).rstrip()
ROW = re.compile(r'(\d+) (\S+) headchef R \d+ (\d+)%')
def rows(n):
    out = []
    for i in range(2):
        m = ROW.fullmatch(re.sub(r'\s+', ' ', line(ppm(n), 29 + i * 18)))
        out.append((m.group(2), int(m.group(3)), int(m.group(1))) if m else None)
    return out
bad = []
head = line(ppm('s1'), 4)
if head != 'PID  PROGRAM          COOK       ST  PAGES  CPU': bad.append(f'the heading "{head}"')
r1, r2, r3 = rows('s1'), rows('s2'), rows('s3')
if None in r1 or [r[0] for r in r1] != ['expo.elf', 'glutton.elf']: bad.append(f's1 by pid: {r1}')
else:
    if r1[1][1] < 50: bad.append(f's1: glutton at {r1[1][1]}%')
    if r1[0][1] > 10: bad.append(f's1: expo at {r1[0][1]}%')
    if r1[0][2] > r1[1][2]: bad.append(f's1: pids out of order {r1}')
if None in r2 or [r[0] for r in r2] != ['glutton.elf', 'expo.elf']: bad.append(f's2 by CPU: {r2}')
if None in r3 or [r[0] for r in r3] != ['expo.elf', 'glutton.elf']: bad.append(f's3 by pid again: {r3}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
[ "$fail" = 0 ] && echo "  ok    CPU in expo: glutton near 100%, expo near 0; c sorts by CPU (glutton first), c again by pid" \
    || echo "  FAIL  CPU in expo"
exit $fail

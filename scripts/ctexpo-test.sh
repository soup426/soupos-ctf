#!/usr/bin/env bash
# ctexpo-test.sh - expo.elf, the processes in a window (v0.60.168), and the
# kernel's rule behind its k (SYS_KILL). As the headchef: glutton.elf in
# the background, expo lists it (pid, program, cook) and k ends it. As
# saucier: a glutton cooked through chef (so the headchef's), expo lists
# it, and k is refused by the kernel ("not yours"), the glutton still
# there. Rows read back against the font; the kernel's log lines.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctexpo.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
DESK=("$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "cook expo.elf" "WAIT:3")
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1"
      "cook glutton.elf &" "WAIT:1" "countertop" "WAIT:2" "${DESK[@]}" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "TYPE:k" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1" "TYPE:q" "WAIT:2"
      "MON:mouse_move 88 0" "WAIT:1" "${CLICK[@]}" "WAIT:2" "clockout" "WAIT:2"
      "saucier" "basil" "WAIT:2" "chef cook glutton.elf &" "WAIT:1" "rosemary" "WAIT:2" "countertop" "WAIT:2"
      "${DESK[@]}" "TYPE:k" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1" "TYPE:q" "WAIT:2"
      "MON:mouse_move 88 0" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
rows = lambda at: [re.sub(r'\s+', ' ', line(at, 29 + i * 18)) for i in range(4)]
foot = lambda at: line(at, 280)
bad = []
r1 = rows(ppm('s1'))
if not re.fullmatch(r'\d+ glutton\.elf headchef R \d+', r1[0]) or not r1[1].split(' ')[1:3] == ['expo.elf', 'headchef']:
    bad.append('s1: ' + ' | '.join(r1[:2]))
s2 = ppm('s2')
if not re.fullmatch(r'ended \d+', foot(s2)): bad.append(f's2: the foot says "{foot(s2)}"')
if any('glutton' in r for r in rows(s2)): bad.append('s2: glutton still listed')
s3 = ppm('s3')
if not re.fullmatch(r'\d+ is not yours to end', foot(s3)): bad.append(f's3: the foot says "{foot(s3)}"')
r3 = rows(s3)
if not re.fullmatch(r'\d+ glutton\.elf headchef R \d+', r3[0]) or 'saucier' not in r3[1]: bad.append('s3: ' + ' | '.join(r3[:2]))
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[proc\] (headchef ends|saucier may not end) [0-9]+ \(asked by /expo\.elf\)$' "$WORK/log" | sed 's/ [0-9]* (/ N (/' > "$WORK/events"
printf '[proc] headchef ends N (asked by /expo.elf)\n[proc] saucier may not end N (asked by /expo.elf)\n' | cmp -s - "$WORK/events" \
    || { echo "        the kernel said: $(grep -E 'ends|may not end' "$WORK/log" | tr '\n' ';')"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    expo.elf: the processes listed with their cooks; k ends the headchef's own; as saucier, the kernel refuses the headchef's" \
    || echo "  FAIL  expo.elf"
exit $fail

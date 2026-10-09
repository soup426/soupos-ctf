#!/usr/bin/env bash
# ctnote-test.sh - notes from programs on the bar (v0.60.170). Two ovens of
# three seconds, one minimised: at zero each puts "oven: done" on the bar
# (SYS_NOTE), the second queued behind the first and shown after it; then
# the bar is clear. The note read against the font, the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctnote.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1")
QUICK=$'MON:mouse_button 1\nmouse_button 0'
# the second oven's window at 268,98: its minimise box at 470-486, 102-118
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "cook oven.elf 3 & cook oven.elf 3 &" "WAIT:1" "MON:mouse_move 278 98" "WAIT:1" "$QUICK"
      "WAIT:3" "MON:screendump $WORK/s1.ppm" "WAIT:12" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move -190 -98" "WAIT:1" "$QUICK" "WAIT:1" "$QUICK" "WAIT:3")
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
def note(at):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(480 + c*8 + b, 4 + j) == 0xF0B040) for j in range(14)), '?')
                   for c in range(45)).rstrip()
bad = []
n1, n2 = note(ppm('s1')), note(ppm('s2'))
if n1 != 'oven: done': bad.append(f'at zero the bar says "{n1}"')
if n2: bad.append(f'after both notes the bar still says "{n2}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
[ "$(grep -cx '\[countertop\] note: oven: done' "$WORK/log")" = 2 ] || { echo "        notes shown: $(grep -c 'note: ' "$WORK/log")"; fail=1; }
grep -E '^\[countertop\] minimise oven' "$WORK/log" >/dev/null || { echo "        no oven was minimised"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    notes on the bar: two ovens (one minimised) each say oven: done there, one after the other, then the bar is clear" \
    || echo "  FAIL  notes on the bar"
exit $fail

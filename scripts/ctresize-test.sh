#!/usr/bin/env bash
# ctresize-test.sh - terminals that change size (v0.60.162). Terminal 1's
# bottom-right corner dragged 160 right and 80 up makes it 100x20; a
# 90-character line slurped there sits on one row (it would wrap at 80),
# read back against the font at the new width; dragged far up and left it
# stops at 40x10 with the prompt still in view.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctresize.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
LONG=$(printf 'abcdefghij%.0s' 1 2 3 4 5 6 7 8 9)
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 495 466" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 80 -40" "WAIT:1" "MON:mouse_move 80 -40" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "slurp $LONG" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_button 1" "WAIT:1" "MON:mouse_move -300 -200" "WAIT:1" "MON:mouse_move -300 -200" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move 33 12" "WAIT:1" "${CLICK[@]}" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h "$LONG" <<'PY' || fail=1
import sys, re
work, fonth, long_ = sys.argv[1], sys.argv[2], sys.argv[3]
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
def term(at, cols, rows):
    return [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(61 + c*8 + b, 83 + r*16 + j) != 0) for j in range(14)), '?')
                    for c in range(cols)).rstrip() for r in range(rows)]
bad = []
t1 = term(ppm('s1'), 100, 20)
if long_ not in t1: bad.append('s1: the 90 characters are not one row at 100 wide: ' + ' | '.join(r for r in t1 if r)[:400])
s2 = ppm('s2')
t2 = term(s2, 40, 10)
if not any(r.startswith('headchef@soupOS:/>') for r in t2): bad.append('s2: no prompt in the 40x10 terminal: ' + ' | '.join(t2))
if s2(61 + 40 * 8 + 3, 200) == 0x000000: bad.append('s2: still black right of 40 columns (not shrunk)')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] resize' "$WORK/log" > "$WORK/events"
printf '[countertop] resize Terminal 1 to 100x20\n[countertop] resize Terminal 1 to 40x10\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    terminals that change size: a corner drag to 100x20 (a 90-character line on one row), then down to 40x10 with the prompt kept" \
    || echo "  FAIL  terminals that change size"
exit $fail

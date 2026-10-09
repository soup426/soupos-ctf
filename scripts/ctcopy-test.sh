#!/usr/bin/env bash
# ctcopy-test.sh - copy and paste in terminal windows (v0.60.160). In
# Terminal 1 a drag over "slurp hello-paste" (row 3, columns 19-35)
# selects it, shown inverted, and copies it; a right click in Terminal 2
# types it there, and Enter runs it (hello-paste comes back); a click
# without a drag in Terminal 1 clears the selection. From screendumps,
# the cells read back against the font, and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcopy.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Terminal 1's cells start at 61,83, Terminal 2's at 89,111; 8x16 each
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "slurp hello-paste" "WAIT:2"
      "MON:mouse_move 17 127" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 64 0" "WAIT:1" "MON:mouse_move 64 0" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move -145 -127" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 200 288" "WAIT:1" "MON:mouse_button 4" "WAIT:1" "MON:mouse_button 0" "WAIT:2" "KEY:ret" "WAIT:2"
      "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move -330 -150" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 218 -138" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def term(at, ox, oy):
    return [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(ox + c*8 + b, oy + r*16 + j) != 0) for j in range(14)), '?')
                    for c in range(80)).rstrip() for r in range(25)]
GREY = 0xAAAAAA
bad = []
cellbg = lambda at, ox, oy, r, c: at(ox + c*8, oy + r*16 + 15)          # a cell's last row: no glyph there
s1 = ppm('s1')
for c in (19, 27, 35):
    if cellbg(s1, 61, 83, 3, c) != GREY: bad.append(f's1: cell 3,{c} not inverted ({cellbg(s1, 61, 83, 3, c):06x})')
for c in (10, 38):                                                   # 36-37 are under the pointer
    if cellbg(s1, 61, 83, 3, c) != 0: bad.append(f's1: cell 3,{c} inverted, outside the drag')
t2 = term(ppm('s2'), 89, 111)
if not any(r.endswith('> slurp hello-paste') for r in t2): bad.append('s2: the paste is not on Terminal 2\'s prompt: ' + ' | '.join(r for r in t2 if r)[:300])
if 'hello-paste' not in t2: bad.append('s2: Enter did not run it (no hello-paste line)')
s3 = ppm('s3')
if cellbg(s3, 61, 83, 3, 27) == GREY: bad.append('s3: the selection is still shown after a plain click')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (copied|pasted)' "$WORK/log" > "$WORK/events"
printf '[countertop] copied 17 bytes from Terminal 1\n[countertop] pasted 17 bytes into Terminal 2\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    copy and paste: a drag selects and copies (inverted), a right click pastes into another terminal and Enter runs it, a click clears it" \
    || echo "  FAIL  copy and paste in terminal windows"
exit $fail

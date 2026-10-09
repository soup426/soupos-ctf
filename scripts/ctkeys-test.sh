#!/usr/bin/env bash
# ctkeys-test.sh - keyboard shortcuts on the desktop (v0.60.171). F4
# reached no one before (the scancode table has 0 for it); F1-F12 are key
# codes now. Ctrl+Alt+T opens a terminal; Alt+F4 closes the focused window: the
# terminal, then Pantry under it; with frost focused, Alt+F4 asks frost to
# close (WEV_CLOSE) and it does. From the log and a screendump.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctkeys.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "KEY:ctrl-alt-t" "WAIT:2" "slurp shortcut" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "KEY:alt-f4" "WAIT:2" "KEY:alt-f4" "WAIT:2"
      "KEY:ctrl-alt-t" "WAIT:2" "cook frost.elf" "WAIT:3" "KEY:alt-f4" "WAIT:3"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 288 12" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:3")
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
d = open(f'{work}/s1.ppm', 'rb').read(); i = 0; words = []
while len(words) < 4:
    while d[i:i+1].isspace(): i += 1
    j = i
    while not d[j:j+1].isspace(): j += 1
    words.append(d[i:j]); i = j
px = d[i + 1:]; W = int(words[1])
at = lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
t = [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(61 + c*8 + b, 83 + r*16 + j) != 0) for j in range(14)), '?')
             for c in range(80)).rstrip() for r in range(25)]
if 'shortcut' not in t: print('        the Ctrl+Alt+T terminal: ' + ' | '.join(r for r in t if r)[:300]); sys.exit(1)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (ctrl-alt-t|alt-f4|open Terminal|close |close asked|program closed)' "$WORK/log" | sed 's/ for pid.*//' > "$WORK/events"
printf '%s\n' '[countertop] ctrl-alt-t' '[countertop] open Terminal 1' '[countertop] alt-f4 Terminal 1' '[countertop] close Terminal 1' \
       '[countertop] alt-f4 Pantry' '[countertop] close Pantry' '[countertop] ctrl-alt-t' '[countertop] open Terminal 2' \
       '[countertop] alt-f4 frost' '[countertop] program closed frost' '[countertop] close frost' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    shortcuts: Ctrl+Alt+T opens a terminal; Alt+F4 closes the focused window (a terminal, Pantry) and asks frost to close" \
    || echo "  FAIL  keyboard shortcuts"
exit $fail

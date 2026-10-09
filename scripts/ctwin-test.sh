#!/usr/bin/env bash
# ctwin-test.sh - countertop's windows (v0.60.142), from screendumps and the
# log: Pantry opens over Welcome and has the focus; a click on Welcome's
# title raises it (its body over the overlap, its title blue, Pantry's
# grey); a drag by the title moves it 100,80 and what it uncovered is the
# background again; the close box closes Pantry, uncovering the background.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctwin.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 150 110" "WAIT:1"
      "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 100 80" "WAIT:1" "MON:mouse_button 0" "WAIT:1" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 518 82" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "KEY:esc" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys
work = sys.argv[1]
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
def c_div(a, b): return -((-a) // b) if (a < 0) != (b < 0) and a % b else a // b
def grad(y, TOP=0x1E3A5F, BOT=0xD87A3E, BH=24, H=768):
    out = 0
    for s in (16, 8, 0):
        a, b = (TOP >> s) & 255, (BOT >> s) & 255
        out |= (a + c_div((b - a) * (y - BH), H - BH - 1)) << s
    return out
WEL, PAN, FOC, BLUR = 0xF4F1EA, 0xE6EEF5, 0x3A6EA5, 0x5A5F66
checks = {
  's1': [((500, 300), PAN, 'Pantry over the overlap'), ((300, 105), BLUR, 'Welcome title grey'), ((600, 265), FOC, 'Pantry title blue')],
  's2': [((500, 300), WEL, 'Welcome raised over the overlap'), ((300, 105), FOC, 'Welcome title blue'), ((700, 265), BLUR, 'Pantry title grey')],
  's3': [((400, 185), FOC, 'Welcome title at its new place'), ((130, 130), grad(130), 'background where Welcome was'), ((500, 300), WEL, 'Welcome body moved')],
  's4': [((700, 450), grad(450), 'background where Pantry was'), ((400, 185), FOC, 'Welcome still focused')],
}
bad = []
for n, cs in checks.items():
    at = ppm(n)
    for (x, y), want, what in cs:
        if at(x, y) != want: bad.append(f'{n}: {what}: {at(x,y):06x} at {x},{y}, not {want:06x}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" | grep -E '^\[countertop\] (raise|moved|close)' > "$WORK/events"
printf '[countertop] raise Welcome\n[countertop] moved Welcome to 220,180\n[countertop] close Pantry\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    countertop's windows: Pantry opens on top, a click raises Welcome, a title-bar drag moves it 100,80, the close box closes Pantry; the background comes back where they were" \
    || echo "  FAIL  countertop's windows"
exit $fail

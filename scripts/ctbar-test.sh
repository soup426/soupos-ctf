#!/usr/bin/env bash
# ctbar-test.sh - countertop's taskbar (v0.60.144), from screendumps and
# the log: its launcher opens a terminal; the window's minimise box hides
# it (the focus to Pantry, its button dim, the background where it was);
# its button restores it, then minimises it again (focused); another
# window's button raises that window.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctbar.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"; D="MON:mouse_move 0 120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 40 120" "$D" "$D" "$D" "$D" "$D" "MON:mouse_move 0 34" "WAIT:1" "${CLICK[@]}"
      "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 120 -120" "MON:mouse_move 120 -120" "MON:mouse_move 120 -120" "MON:mouse_move 120 -120" "MON:mouse_move 120 -120"
      "MON:mouse_move 28 -84" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move -120 120" "MON:mouse_move -48 120" "$D" "$D" "$D" "MON:mouse_move 0 84" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "${CLICK[@]}" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move -120 0" "MON:mouse_move -120 0" "MON:mouse_move -60 0" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s5.ppm" "WAIT:1"
      "MON:mouse_move 0 -400" "WAIT:1" "KEY:esc" "WAIT:2")
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
FOC, BLUR, MIN, BTN = 0x3A6EA5, 0x5A5F66, 0x2A2E34, 0x3A3F48
checks = {
  's1': [((100, 300), 0x000000, 'Terminal 1 open over the desktop'), ((560, 746), FOC, 'its taskbar button blue')],
  's2': [((100, 300), grad(300), 'the background where Terminal 1 was'), ((560, 746), MIN, 'its button dim'),
         ((600, 265), FOC, 'Pantry has the focus'), ((410, 746), FOC, "Pantry's button blue")],
  's3': [((100, 300), 0x000000, 'Terminal 1 back'), ((560, 746), FOC, 'its button blue again'), ((720, 265), BLUR, 'Pantry grey (its title right of Terminal 1)')],
  's4': [((100, 300), grad(300), 'Terminal 1 minimised from its button'), ((560, 746), MIN, 'its button dim')],
  's5': [((300, 105), FOC, 'Welcome raised from its button'), ((250, 746), FOC, "Welcome's button blue")],
}
bad = []
for n, cs in checks.items():
    at = ppm(n)
    for (x, y), want, what in cs:
        if at(x, y) != want: bad.append(f'{n}: {what}: {at(x,y):06x} at {x},{y}, not {want:06x}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" | grep -E '^\[countertop\] (open|minimise|restore|raise)' > "$WORK/events"
printf '[countertop] open Terminal 1\n[countertop] minimise Terminal 1\n[countertop] restore Terminal 1\n[countertop] minimise Terminal 1\n[countertop] raise Welcome\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the taskbar: the launcher opens a terminal, the minimise box hides it (focus to Pantry), its button restores and minimises it, Welcome's button raises Welcome" \
    || echo "  FAIL  countertop's taskbar"
exit $fail

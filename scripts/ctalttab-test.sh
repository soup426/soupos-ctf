#!/usr/bin/env bash
# ctalttab-test.sh - Alt+Tab on the desktop (v0.60.155). Welcome, Pantry
# and a terminal, the terminal on top: Alt+Tab gives Pantry the focus, a
# second gives the third, Welcome, and a third brings the terminal back;
# with Pantry minimised, Alt+Tab passes over it to Welcome. Read from the
# title bars and the taskbar's blue button in screendumps, and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctalttab.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 0 400" "WAIT:1"
      "KEY:alt-tab" "WAIT:1" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "KEY:alt-tab" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "KEY:alt-tab" "WAIT:1" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 548 -140" "WAIT:1" "${CLICK[@]}"
      "KEY:alt-tab" "WAIT:1" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move -460 -260" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
FOC, BLUR, MIN, BTN = 0x3A6EA5, 0x5A5F66, 0x2A2E34, 0x3A3F48
# title bars: Welcome 120,100 (420 wide), Pantry 420,260 (360 wide), Terminal 1 60,60
WEL, PAN, TERM = (300, 105), (720, 265), (100, 65)
# taskbar buttons: Welcome, Pantry, Terminal 1
BW, BP, BT = (250, 746), (410, 746), (560, 746)
checks = {
  's1': [(PAN, FOC, 'Pantry focused'), (BP, FOC, "Pantry's button blue"), (BT, BTN, "Terminal 1's button plain")],
  's2': [(WEL, FOC, 'Welcome focused (the third)'), (BW, FOC, "Welcome's button blue"), (BP, BTN, "Pantry's button plain")],
  's3': [(TERM, FOC, 'Terminal 1 focused again'), (BT, FOC, "Terminal 1's button blue")],
  's4': [(WEL, FOC, 'Welcome focused, minimised Pantry passed over'), (BW, FOC, "Welcome's button blue"), (BP, MIN, "Pantry's button dim")],
}
bad = []
for n, cs in checks.items():
    at = ppm(n)
    for (x, y), want, what in cs:
        if at(x, y) != want: bad.append(f'{n}: {what}: {at(x,y):06x} at {x},{y}, not {want:06x}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (alt-tab|minimise)' "$WORK/log" > "$WORK/events"
printf '[countertop] alt-tab to Pantry\n[countertop] alt-tab to Welcome\n[countertop] alt-tab to Terminal 1\n[countertop] minimise Pantry\n[countertop] alt-tab to Welcome\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    Alt+Tab: the next window down, then the third, then round again; a minimised window passed over" \
    || echo "  FAIL  Alt+Tab"
exit $fail

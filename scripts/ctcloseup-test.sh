#!/usr/bin/env bash
# ctcloseup-test.sh - closing the kitchen from the desktop (v0.60.183).
# A: the headchef, a terminal with `whoami` typed, then the Kitchen menu's
#    "close up": QEMU (without -no-shutdown) exits by itself, and the
#    terminal's leftovers reached /etc/LEFTOVER before the power went.
# B: saucier's "close up" is refused, said on the bar, QEMU still running.
# C: the headchef's "reheat" (without -no-reboot): the login twice.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcloseup.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TOP=("$L" "$L" "$L" "$L" "$L")
# the Kitchen button at 392-464; its rows from y 26, 24 apart: close up the 9th (y 230), reheat the 10th (y 254)
boot() {                                  # boot <name> <qemu args...>
    local n=$1; shift
    cp disk.img "$WORK/$n.img"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/$n.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -display none "$@" \
        -serial "file:$WORK/$n.log" -monitor "unix:$WORK/$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
}
# A
boot a -no-reboot
python3 scripts/qemu_keys.py "$WORK/a.sock" "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" \
    "${TOP[@]}" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" "whoami" "WAIT:2" \
    "MON:mouse_move 220 0" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 0 218" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" \
    >/dev/null 2>&1                       # QEMU may be gone before the driver lets go: what follows decides
for i in $(seq 1 100); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
if kill -0 "$PID" 2>/dev/null; then echo "        a: QEMU still running 10 s after close up"; fail=1; kill "$PID"; fi
wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/a.img" ::/etc/LEFTOVER "$WORK/a.left" 2>/dev/null
grep -qx 'whoami' "$WORK/a.left" 2>/dev/null || { echo "        a: whoami not in /etc/LEFTOVER ($(tr '\n' ';' < "$WORK/a.left" 2>/dev/null))"; fail=1; }
# B
boot b -no-reboot -no-shutdown
python3 scripts/qemu_keys.py "$WORK/b.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "clockout" "WAIT:2" \
    "saucier" "basil" "WAIT:2" "countertop" "WAIT:2" \
    "${TOP[@]}" "MON:mouse_move 420 12" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 0 218" "WAIT:1" \
    "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1" "MON:screendump $WORK/b.ppm" "WAIT:1" \
    >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (b)"; fail=1; }
kill -0 "$PID" 2>/dev/null || { echo "        b: QEMU went"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/b.ppm" src/font8x16.h <<'PY' || fail=1
import sys, re
path, fonth = sys.argv[1], sys.argv[2]
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127):
    shape.setdefault(font[c][:14], chr(c))
d = open(path, 'rb').read(); i = 0; words = []
while len(words) < 4:
    while d[i:i+1].isspace(): i += 1
    j = i
    while not d[j:j+1].isspace(): j += 1
    words.append(d[i:j]); i = j
px = d[i + 1:]; W = int(words[1])
at = lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
note = ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(480 + c*8 + b, 4 + j) == 0xF0B040) for j in range(14)), '?') for c in range(40)).rstrip()
if note != 'only the headchef may close the kitchen': print(f'        b: the bar says "{note}"'); sys.exit(1)
PY
# C
boot c -no-shutdown
python3 scripts/qemu_keys.py "$WORK/c.sock" "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" \
    "${TOP[@]}" "MON:mouse_move 420 12" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 0 242" "WAIT:1" "${CLICK[@]}" "WAIT:8" \
    >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (c)"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for n in a b c; do tr -d '\r' < "$WORK/$n.log" > "$WORK/$n.txt"; done
[ "$(grep -c 'clock in to start your shift' "$WORK/c.txt")" = 2 ] || { echo "        c: the login $(grep -c 'clock in to start your shift' "$WORK/c.txt") times"; fail=1; }
grep -hE '^\[(countertop|power)\] (closing|reheating|saucier may not|off|reboot)' "$WORK/a.txt" "$WORK/b.txt" "$WORK/c.txt" > "$WORK/events"
printf '%s\n' '[countertop] closing the kitchen' '[power] off' '[countertop] saucier may not close the kitchen' \
       '[countertop] reheating the kitchen' '[power] reboot' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    closing the kitchen: the headchef's close up powers QEMU off (leftovers saved first); saucier refused, on the bar; reheat boots again" \
    || echo "  FAIL  closing the kitchen"
exit $fail

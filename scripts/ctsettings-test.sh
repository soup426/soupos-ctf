#!/usr/bin/env bash
# ctsettings-test.sh - settings that stay (v0.60.169). The Kitchen menu's
# backdrop turns the headchef's background from dusk to sea and keeps it
# in /countertop; Leave, countertop again, and it is sea; saucier's
# desktop is still dusk. The gradients compared pixel for pixel with the
# desktop's own blend; the file on the disk and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctsettings.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# Kitchen at 420,12; its ninth row (backdrop) at y 230 (v0.60.245: neosim too, one row more) (v0.60.239: mincecraft and doom above it, two rows more); Leave at 288,12
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 420 12" "WAIT:1" "${CLICK[@]}" "MON:mouse_move 0 218" "WAIT:1" "${CLICK[@]}"
      "MON:screendump $WORK/s1.ppm" "WAIT:1" "MON:mouse_move -132 -218" "WAIT:1" "${CLICK[@]}" "WAIT:1"
      "countertop" "WAIT:3" "MON:screendump $WORK/s2.ppm" "WAIT:1" "${CLICK[@]}" "WAIT:1"
      "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "countertop" "WAIT:3" "MON:screendump $WORK/s3.ppm" "WAIT:1" "${CLICK[@]}" "WAIT:2")
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
def grad(y, top, bot, BH=24, H=768):
    out = 0
    for s in (16, 8, 0):
        a, b = (top >> s) & 255, (bot >> s) & 255
        out |= (a + c_div((b - a) * (y - BH), H - BH - 1)) << s
    return out
DUSK, SEA = (0x1E3A5F, 0xD87A3E), (0x0B2E4A, 0x3FA7B8)
bad = []
for n, (name, g) in (('s1', ('sea', SEA)), ('s2', ('sea, kept', SEA)), ('s3', ("saucier's dusk", DUSK))):
    at = ppm(n)
    for y in (100, 400, 700):
        if at(1000, y) != grad(y, *g): bad.append(f'{n} ({name}): {at(1000,y):06x} at 1000,{y}, not {grad(y, *g):06x}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
[ "$(mcopy -n -i "$WORK/disk.img" ::/countertop - 2>/dev/null)" = "background 1" ] || { echo "        /countertop holds: $(mcopy -n -i "$WORK/disk.img" ::/countertop - 2>&1 | head -2)"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (background|settings)' "$WORK/log" > "$WORK/events"
printf '[countertop] background sea, kept in /countertop\n[countertop] settings from /countertop: background sea\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    settings that stay: backdrop to sea, kept in /countertop and read back by the next desktop; saucier's stays dusk" \
    || echo "  FAIL  settings that stay"
exit $fail

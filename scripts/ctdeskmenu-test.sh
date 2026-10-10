#!/usr/bin/env bash
# ctdeskmenu-test.sh - a menu on the background (v0.60.184). A right click
# on the background at 800,600 drops the menu there (its five entries read
# back); each entry does what its button does: Terminal, Files, Kitchen
# (the Kitchen menu), backdrop, cloche. Esc puts it up, and so does a click
# away; a right click on a window or an icon opens nothing.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctdeskmenu.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
RIGHT=("MON:mouse_button 2" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
# from 800,600 (the menu's corner) to entry i's middle, 810,614+24i, and back
pick() { local dy=$(( 14 + 24 * $1 )); printf '%s\n' "MON:mouse_move 10 $dy" "WAIT:1" "${CLICK[@]}" "MON:mouse_move -10 -$dy" "WAIT:1"; }
mapfile -t keys < <(printf '%s\n' "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" \
    "MON:mouse_move 400 300" "MON:mouse_move 400 300" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move -100 0" "WAIT:1" "MON:screendump $WORK/s1.ppm" "MON:mouse_move 100 0" "WAIT:1"
    pick 0; printf '%s\n' "${RIGHT[@]}"; pick 1; printf '%s\n' "${RIGHT[@]}"; pick 2
    printf '%s\n' "${CLICK[@]}" "${RIGHT[@]}"; pick 3
    printf '%s\n' "${RIGHT[@]}" "KEY:esc" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
    printf '%s\n' "${RIGHT[@]}" "MON:mouse_move -300 100" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1" "MON:mouse_move 300 -100" "WAIT:1"
    printf '%s\n' "${RIGHT[@]}"; pick 4; printf '%s\n' "rosemary" "WAIT:2"
    printf '%s\n' "MON:mouse_move -500 -400" "WAIT:1" "${RIGHT[@]}" "MON:mouse_move -260 -140" "WAIT:1" "${RIGHT[@]}" "WAIT:1")
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
def word(at, x, y, n):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(x + c*8 + b, y + j) == 0x202020) for j in range(14)), '?')
                   for c in range(n)).rstrip()
bad = []
s1 = ppm('s1')
got = [word(s1, 810, 600 + 2 + i * 24 + 4, 8) for i in range(5)]
if got != ['Terminal', 'Files', 'Kitchen', 'backdrop', 'cloche']: bad.append(f's1: the menu reads {got}')
for n in ('s2', 's3'):
    if ppm(n)(900, 660) == 0xF4F1EA: bad.append(f'{n}: the menu is still there')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (background menu|open Terminal|open Files|kitchen menu|background [a-z]+, |cloche (down|lifted))' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] background menu at 800,600' '[countertop] background menu: Terminal' '[countertop] open Terminal 1' \
       '[countertop] background menu at 800,600' '[countertop] background menu: Files' '[countertop] open Files at /' \
       '[countertop] background menu at 800,600' '[countertop] background menu: Kitchen' '[countertop] kitchen menu open' '[countertop] kitchen menu closed' \
       '[countertop] background menu at 800,600' '[countertop] background menu: backdrop' '[countertop] background sea, kept in /countertop' \
       '[countertop] background menu at 800,600' '[countertop] background menu at 800,600' \
       '[countertop] background menu at 800,600' '[countertop] background menu: cloche' '[countertop] cloche down (the background menu)' '[countertop] cloche lifted' \
    | cmp -s - "$WORK/events" || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the background's menu: read back where it was right-clicked; Terminal, Files, Kitchen, backdrop and cloche each do their thing; Esc and a click away put it up; not over a window or an icon" \
    || echo "  FAIL  the background's menu"
exit $fail

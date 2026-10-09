#!/usr/bin/env bash
# ctfiles-test.sh - countertop's files window (v0.60.145), from screendumps
# and the log. The bar's Files button lists / (bowls first, files with
# their sizes); a double click on etc/ enters it, on .. comes back; a
# double click on HELLO.TXT opens a terminal with it in jot (v0.60.154: a
# text file goes to the editor, the rest to pour), Esc leaves it; the
# arrows scroll the list down to "A Long Recipe Name.txt" and Enter opens
# that in jot too (the path quoted, spaces and all). The window's rows and the terminals' cells
# are read back against font8x16. The Files click and the .. double click
# are pressed and released inside one monitor write, shorter than the
# desktop loop's nap: before v0.60.145 such a click was lost.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctfiles.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=("MONQ:mouse_button 1" "MONQ:mouse_button 0" "MONQ:mouse_button 1" "MONQ:mouse_button 0" "WAIT:2")
# a click and a double click inside one write, a millisecond or so: far
# shorter than the desktop loop's 15 ms nap, so only the IRQ's count sees them
QUICK=$'MON:mouse_button 1\nmouse_button 0'
QUICK2=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
DOWN=(); for i in $(seq 22); do DOWN+=("KEY:down"); done
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 350 12" "WAIT:1" "$QUICK" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move 350 108" "WAIT:1" "${TWICE[@]}" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "$QUICK2" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move 0 18" "WAIT:1" "${TWICE[@]}" "WAIT:2" "MON:screendump $WORK/s4.ppm" "WAIT:1" "KEY:esc" "WAIT:1"
      "MON:mouse_move 200 -48" "WAIT:1" "${CLICK[@]}" "${DOWN[@]}" "WAIT:1" "MON:screendump $WORK/s5.ppm" "WAIT:1"
      "KEY:ret" "WAIT:3" "MON:screendump $WORK/s6.ppm" "WAIT:1"
      "MON:mouse_move -600 -78" "WAIT:1" "${CLICK[@]}" "WAIT:2" "hash FBHELLO" "UNTIL:@soupOS:")
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
INK = {0x202020, 0x1E4E8C, 0xF0F0F0}            # a row's text: a file, a bowl, selected
def word(at, ox, oy, n=30):
    s = ''
    for c in range(n):
        g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + j) in INK) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
def term(at, ox, oy):
    rows = []
    for r in range(25):
        line = ''
        for c in range(80):
            g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + r*16 + j) != 0) for j in range(14))
            line += shape.get(g, '?')
        rows.append(line.rstrip())
    return rows
# the window: 604,60, 380 wide; the path at 613,87; row i's text at 615,112+18i
PATH, ROW = (613, 87), lambda i: (615, 112 + 18 * i)
FOC = 0x3A6EA5
bad = []
def want(got, exp, what):
    if got != exp: bad.append(f'{what}: "{got}", not "{exp}"')
s1 = ppm('s1')
want(word(s1, *PATH), '/', 's1 path'); want(word(s1, *ROW(0)), 'etc/', 's1 row 0 (bowls first)')
want(word(s1, *ROW(1)), 'HELLO.TXT', 's1 row 1'); want(word(s1, 957, 130, 2), '47', "s1 HELLO.TXT's size")
want(s1(800, 120), FOC, 's1 row 0 selected')
s2 = ppm('s2')
want(word(s2, *PATH), '/etc', 's2 path (etc/ double clicked)'); want(word(s2, *ROW(0)), '..', 's2 row 0')
s3 = ppm('s3')
want(word(s3, *PATH), '/', 's3 path (.. double clicked)'); want(word(s3, *ROW(0)), 'etc/', 's3 row 0')
t1 = term(ppm('s4'), 61, 83)
if t1[:2] != ['Hello from soupOS!', 'A warm bowl of kernel soup.']:              # jot's first two rows
    bad.append('Terminal 1: jot does not show HELLO.TXT: ' + ' | '.join(t1[:3]))
s5 = ppm('s5')                                   # 22 rows shown: row 23 scrolled to the bottom
want(word(s5, *ROW(21)), 'A Long Recipe Name.txt', 's5 last row (scrolled)'); want(s5(800, 112 + 18 * 21 + 8), FOC, 's5 last row selected')
want(word(s5, *ROW(0)), 'README.TXT', 's5 first row (scrolled by two)')
t2 = term(ppm('s6'), 89, 111)
if t2[0] != 'A file whose name does not fit in 8.3 at all.':
    bad.append('Terminal 2: jot does not show the long name: ' + ' | '.join(t2[:2]))
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (open Files|files in|pour|jot)' "$WORK/log" | sed 's/, [0-9]* entries//' > "$WORK/events"
printf '[countertop] open Files at /\n[countertop] files in /etc\n[countertop] files in /\n[countertop] jot /HELLO.TXT\n[countertop] jot /A Long Recipe Name.txt\n' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not give the console back"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the files window: / with bowls first and sizes, a double click enters etc/ and .. comes back, HELLO.TXT opens in jot in a terminal, the arrows scroll and Enter opens a name with spaces in jot" \
    || echo "  FAIL  countertop's files window"
exit $fail

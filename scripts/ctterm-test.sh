#!/usr/bin/env bash
# ctterm-test.sh - countertop's terminal windows (v0.60.143). The bar's
# Terminal button opens a window with a shell of its own; a second has
# another; each runs what is typed into it while it has the focus; clockout
# in one closes its window and ends its session; a click gives the first
# the keyboard back; the bar's Leave button ends the desktop and every
# session in it. What each window shows is read back from screendumps cell
# by cell against font8x16 (the text in each cell, not just pixels).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctterm.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "slurp one-window" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "${CLICK[@]}" "cd /etc" "WAIT:1" "pwd" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "clockout" "WAIT:3" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "MON:mouse_move -100 60" "WAIT:1" "${CLICK[@]}" "slurp back in one" "WAIT:2" "MON:screendump $WORK/s4.ppm" "WAIT:1"
      "MON:mouse_move 170 -60" "WAIT:1" "${CLICK[@]}" "WAIT:2" "hash FBHELLO" "UNTIL:@soupOS:")
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
# the top 14 rows of each glyph: the cursor is an underline in the last two
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
def read(n, ox, oy):
    """The 80x25 cells of a terminal window whose body starts at ox,oy, as text."""
    at = ppm(n); rows = []
    for r in range(25):
        line = ''
        for c in range(80):
            g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + r*16 + j) != 0) for j in range(14))
            line += shape.get(g, '?')
        rows.append(line.rstrip())
    return rows
# Terminal 1's body: window at 60,60, border 1, title 22; Terminal 2's 28 further on
T1, T2 = (61, 83), (89, 111)
bad = []
def has(rows, text, what):
    if not any(text in r for r in rows): bad.append(f'{what}: no "{text}" in ' + ' | '.join(r for r in rows if r)[:300])
s1 = read('s1', *T1)
has(s1, 'A terminal on the countertop. Welcome to the kitchen, headchef.', 'Terminal 1')
has(s1, 'headchef@soupOS:/> slurp one-window', 'Terminal 1'); has(s1, 'one-window', 'Terminal 1')
s2 = read('s2', *T2)
has(s2, 'headchef@soupOS:/> cd /etc', 'Terminal 2'); has(s2, 'headchef@soupOS:/etc> pwd', 'Terminal 2'); has(s2, '/etc', 'Terminal 2')
if any('one-window' in r for r in s2): bad.append('Terminal 2 shows Terminal 1\'s line')
s3 = read('s3', *T1)                       # Terminal 2 closed: Terminal 1 whole again
has(s3, 'slurp one-window', 'Terminal 1 after Terminal 2 closed')
s4 = read('s4', *T1)
has(s4, 'headchef@soupOS:/> slurp back in one', 'Terminal 1 focused again'); has(s4, 'back in one', 'Terminal 1 focused again')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -c 'session for uid 0 started' "$WORK/log" | grep -x 2 >/dev/null || { echo "        not two sessions started"; fail=1; }
[ "$(grep -c 'session for headchef ended' "$WORK/log")" = 2 ] || { echo "        not both sessions ended: $(grep 'ended' "$WORK/log" | tr '\n' ';')"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null && grep 'AlphaSOUP-32' "$WORK/log" >/dev/null || { echo "        Leave did not give the console back"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    terminal windows: two shells of their own (cd in one is not in the other), clockout closes its window, a click takes the keyboard back, Leave ends both sessions and the desktop" \
    || echo "  FAIL  countertop's terminals"
exit $fail

#!/usr/bin/env bash
# ctneosim-test.sh - "open in neosim" in the files window's menu (v0.60.207),
# as saucier: note.txt's menu has a third entry for a text file, and it
# opens the file in neosim (a terminal types cook neosim.elf PATH, as a
# double click types jot). neosim's window read back against the font: its
# first line and its status line; the desktop's log line.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctneosim.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
RIGHT=("MON:mouse_button 4" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
TWICE=$'MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0'
# As ctfilesmenu-test: the files window at 604,60, rows from y 112, 18 each;
# home/ then saucier/; note.txt the second row (690,144); the menu where the
# right click was, its entries 22 apart: new bowl, remove, open in neosim.
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "clockout" "WAIT:2"
      "saucier" "basil" "WAIT:2" "stir note.txt hello neosim" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 350 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 350 126" "WAIT:1" "$TWICE" "WAIT:2" "$TWICE" "WAIT:2"
      "MON:mouse_move -10 6" "WAIT:1" "${RIGHT[@]}" "MON:screendump $WORK/menu.ppm" "WAIT:1"
      "MON:mouse_move 10 57" "WAIT:1" "${CLICK[@]}" "WAIT:4" "MON:screendump $WORK/neosim.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import re, sys
work, fonth = sys.argv[1], sys.argv[2]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127): shape.setdefault(font[c][:14], chr(c))
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
lum = lambda c: (c >> 16) + (c >> 8 & 255) + (c & 255)
def text(at, x0, y0, cols, ink):
    s = ''
    for c in range(cols):
        g = bytes(sum(1 << (7 - i) for i in range(8) if ink(at(x0 + c*8 + i, y0 + j))) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
m = ppm('menu')
entries = [text(m, 690 + 10, 144 + 2 + i * 22 + 3, 18, lambda c: lum(c) < 200) for i in range(3)]   # draw_ctx: x + 10, y + 2 + i*22 + 3
check(entries[2].startswith('open in neosim'), 'note.txt\'s menu: a third entry, open in neosim: %r' % entries)
check('[countertop] neosim /home/saucier/note.txt' in log, 'the desktop opened it in neosim')
n = ppm('neosim')
first = text(n, 241, 93, 40, lambda c: lum(c) > 380)
check(first.startswith('  1 hello neosim'), 'neosim\'s first line: %r' % first)
st = text(n, 241, 93 + 28 * 16, 80, lambda c: lum(c) < 240)
check('NORMAL' in st and 'note.txt' in st, 'its status line: %r' % st)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "ctneosim-test: PASS" || echo "ctneosim-test: FAIL"
exit "$fail"

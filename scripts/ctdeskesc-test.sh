#!/usr/bin/env bash
# ctdeskesc-test.sh - Esc with the background's menu up (v0.60.208). neosim
# in insert mode; Alt+Tab elsewhere; the background menu opened (a right
# click at 800,600, below every window); two more Alt+Tabs bring neosim
# back. Esc then puts the menu away AND reaches neosim (out of insert mode),
# since the focus has moved since the menu was opened. Esc with the focus
# where it was when the menu opened still only closes the menu
# (ctdeskmenu-test). The status line read against the font, the menu's
# corner (gone), and the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctdeskesc.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
RIGHT=("MON:mouse_button 2" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /D.TXT &" "WAIT:1" "countertop" "WAIT:4"
      "TYPE:i" "WAIT:1" "KEY:alt-tab" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 400 300" "MON:mouse_move 400 300" "WAIT:1" "${RIGHT[@]}"
      "MON:screendump $WORK/menu.ppm" "WAIT:1"
      "KEY:alt-tab" "WAIT:1" "KEY:alt-tab" "WAIT:1" "MON:screendump $WORK/before.ppm" "WAIT:1"
      "KEY:esc" "WAIT:2" "MON:screendump $WORK/after.ppm" "WAIT:1")
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
def status(at):
    s = ''
    for c in range(30):
        g = bytes(sum(1 << (7 - i) for i in range(8) if lum(at(241 + c*8 + i, 93 + 28*16 + j)) < 240) for j in range(14))
        s += shape.get(g, '?')
    return s.strip()
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
MENU = 0xF4F1EA                                   # draw_desk's face, at the menu's corner + 5
m, b, a = ppm('menu'), ppm('before'), ppm('after')
check('background menu at' in log and m(805, 597) == MENU, 'the background menu is up: %06x' % m(805, 597))
tabs = re.findall(r'alt-tab to (.*)', log)
check(tabs and tabs[-1].strip() == 'neosim', 'two Alt+Tabs brought neosim back: %r' % tabs)
check(status(b).startswith('-- INSERT --') and b(805, 597) == MENU, 'before Esc: neosim inserting, the menu still up: %r' % status(b))
check(status(a).startswith('NORMAL'), 'Esc reached neosim: %r' % status(a))
check(a(805, 597) != MENU, 'and put the menu away: %06x' % a(805, 597))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "ctdeskesc-test: PASS" || echo "ctdeskesc-test: FAIL"
exit "$fail"

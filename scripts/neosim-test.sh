#!/usr/bin/env bash
# neosim-test.sh - neosim, a modal editor (v0.60.204), its first part.
# Boot 1 edits /EDIT.TXT ("soup", "broth") with only normal and insert mode:
#   j A " stock" Esc      broth stock
#   k O "first" Esc       first above soup
#   j j o "last" Esc      last at the end (Esc leaves the cursor on its t)
#   k x                   the t of broth, under it (column kept): broh
#   I "> " Esc            > broh stock
#   :wq                   written: first, soup, > broh stock, last
# and a screendump in insert mode reads the status line ("-- INSERT --",
# the name, [+]) and the numbered lines against the font.
# Boot 2: x then :q is refused (changed); :q! leaves without writing.
# It starts at the console with -wait, in the background, before the
# desktop, so its markers reach the serial log; its window is the first
# program window, the body at 241,93: cell c,r at 241 + 8c, 93 + 16r.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosim.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'soup\nbroth\n' > "$WORK/edit.txt"
mcopy -o -i "$WORK/disk.img" "$WORK/edit.txt" ::EDIT.TXT
boot() {   # $1: name, then the keys after the desktop is up
    local n=$1; shift
    local keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /EDIT.TXT &" "WAIT:1" "countertop" "WAIT:4" "$@")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$n.log" -monitor "unix:$WORK/mon-$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$n.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($n)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
E="KEY:esc"
boot edit "TYPE:j" "TYPE:A" "TYPE: stock" "WAIT:1" "MON:screendump $WORK/insert.ppm" "WAIT:1" "$E" \
          "TYPE:k" "TYPE:O" "TYPE:first" "$E" \
          "TYPE:jj" "TYPE:o" "TYPE:last" "$E" \
          "TYPE:k" "TYPE:x" \
          "TYPE:I" "TYPE:> " "$E" "WAIT:1" "MON:screendump $WORK/normal.ppm" "WAIT:1" \
          "TYPE::wq" "KEY:ret" "WAIT:3"
mcopy -n -i "$WORK/disk.img" ::EDIT.TXT "$WORK/after.txt" 2>/dev/null
boot quit "TYPE:x" "TYPE::q" "KEY:ret" "WAIT:1" "MON:screendump $WORK/refused.ppm" "WAIT:1" "TYPE::q!" "KEY:ret" "WAIT:3"
mcopy -n -i "$WORK/disk.img" ::EDIT.TXT "$WORK/after2.txt" 2>/dev/null
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
def row(at, r, ink):                     # cell row r of the body, ink(colour) says which pixels are glyph
    line = ''
    for c in range(80):
        g = bytes(sum(1 << (7 - i) for i in range(8) if ink(at(241 + c*8 + i, 93 + r*16 + j))) for j in range(14))
        line += shape.get(g, '?')
    return line.rstrip()
lum = lambda c: (c >> 16) + (c >> 8 & 255) + (c & 255)
light = lambda c: lum(c) > 380            # light text on the dark page
dark = lambda c: lum(c) < 240             # dark text on the light status line
got = open(f'{work}/after.txt', 'rb').read() if __import__('os').path.exists(f'{work}/after.txt') else b''
check(got == b'first\nsoup\n> broh stock\nlast\n', ':wq wrote the edit: %r' % got)
log = open(f'{work}/serial-edit.log', 'rb').read().decode('latin-1')
check('[neosim] open /EDIT.TXT lines=2' in log, 'opened /EDIT.TXT, two lines')
check(re.search(r'\[neosim\] wrote /EDIT.TXT bytes=29\b', log), 'wrote 29 bytes')
check('[neosim] bye' in log, ':wq ended it')
ins = ppm('insert')
st = row(ins, 28, dark)
check('-- INSERT --' in st and '/EDIT.TXT' in st and '[+]' in st, 'insert mode: the status line: %r' % st)
check(row(ins, 1, light).startswith('  2 broth stock'), 'insert mode: line 2, numbered: %r' % row(ins, 1, light))
nm = ppm('normal')
lines = [row(nm, r, light) for r in range(4)]
check(lines == ['  1 first', '  2 soup', '  3 > broh stock', '  4 last'], 'normal mode: the four lines: %r' % lines)
check('INSERT' not in row(nm, 28, dark), 'normal mode: no INSERT on the status line')
q = open(f'{work}/serial-quit.log', 'rb').read().decode('latin-1')
check('[neosim] refused :q (changed)' in q, ':q with a change refused')
rf = ppm('refused')
check('No write since last change' in row(rf, 29, light), 'and said so on the command line: %r' % row(rf, 29, light))
check('[neosim] bye' in q, ':q! ended it')
check(got and open(f'{work}/after2.txt', 'rb').read() == got, ':q! wrote nothing')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "neosim-test: PASS" || echo "neosim-test: FAIL"
exit "$fail"

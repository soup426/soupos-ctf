#!/usr/bin/env bash
# neosim-help-test.sh - :help (v0.60.219): a read-only page of neosim's keys
# and commands in place of the file. Read back against the font: its title
# and its first groups; x on it refused (E21), :w refused (E382); :q back to
# the file as it was (its first line, no [+]); :h the same page.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimhelp.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'a soup\nof two lines\n' > "$WORK/h.txt"; mcopy -o -i "$WORK/disk.img" "$WORK/h.txt" ::H.TXT
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /H.TXT &" "WAIT:1" "countertop" "WAIT:4"
      "TYPE::help" "KEY:ret" "WAIT:1" "MON:screendump $WORK/help.ppm" "WAIT:1"
      "TYPE:x" "WAIT:1" "MON:screendump $WORK/x.ppm" "WAIT:1"
      "TYPE::w" "KEY:ret" "WAIT:1" "MON:screendump $WORK/w.ppm" "WAIT:1"
      "TYPE::q" "KEY:ret" "WAIT:1" "MON:screendump $WORK/back.ppm" "WAIT:1"
      "TYPE::h" "KEY:ret" "WAIT:1" "MON:screendump $WORK/h.ppm" "WAIT:1")
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
def row(at, r, ink):
    s = ''
    for c in range(80):
        g = bytes(sum(1 << (7 - i) for i in range(8) if ink(at(241 + c*8 + i, 93 + r*16 + j))) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
light = lambda c: lum(c) > 380
dark = lambda c: lum(c) < 240
hp = ppm('help')
page = [row(hp, r, light) for r in range(10)]
check('neosim' in page[0] and ':q' in page[0], 'the page\'s title: %r' % page[0])
check(any(l.strip().startswith('MOVING') for l in page) and any(l.strip().startswith('CHANGING') for l in page), 'its groups (MOVING, CHANGING): %r' % page[:6])
check('help' in row(hp, 28, dark), 'the status line says help: %r' % row(hp, 28, dark))
check('E21' in row(ppm('x'), 29, light), 'x on it refused: %r' % row(ppm('x'), 29, light))
check('E382' in row(ppm('w'), 29, light), ':w on it refused: %r' % row(ppm('w'), 29, light))
bk = ppm('back')
check(row(bk, 0, light).startswith('  1 a soup') and '[+]' not in row(bk, 28, dark), ':q back to the file as it was: %r' % row(bk, 0, light))
check(row(ppm('h'), 0, light) == page[0], ':h the same page')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "neosim-help-test: PASS" || echo "neosim-help-test: FAIL"
exit "$fail"

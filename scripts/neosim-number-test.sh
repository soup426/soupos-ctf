#!/usr/bin/env bash
# neosim-number-test.sh - :set nu / nonu / rnu / nornu (v0.60.236), the
# example in neovim's own :h number_relativenumber, the cursor on its third
# line: each setting's four rows read back against the font, as the table
# there has them (number on by default here; neovim's is off). Then the
# long names (:set number relativenumber), several at once.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimnum.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'apple\npear\nnobody\nthere\n' > "$WORK/n.txt"; mcopy -o -i "$WORK/disk.img" "$WORK/n.txt" ::N.TXT
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /N.TXT &" "WAIT:1" "countertop" "WAIT:4" "TYPE:3G" "WAIT:1"
      "MON:screendump $WORK/nu.ppm" "WAIT:1"
      "TYPE::set rnu" "KEY:ret" "WAIT:1" "MON:screendump $WORK/nurnu.ppm" "WAIT:1"
      "TYPE::set nonu" "KEY:ret" "WAIT:1" "MON:screendump $WORK/rnu.ppm" "WAIT:1"
      "TYPE::set nornu" "KEY:ret" "WAIT:1" "MON:screendump $WORK/none.ppm" "WAIT:1"
      "TYPE::set number relativenumber" "KEY:ret" "WAIT:1" "MON:screendump $WORK/long.ppm" "WAIT:1")
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
def rows(n):
    at = ppm(n); out = []
    for r in range(4):
        s = ''
        for c in range(20):
            g = bytes(sum(1 << (7 - i) for i in range(8) if lum(at(241 + c*8 + i, 93 + r*16 + j)) > 380) for j in range(14))
            s += shape.get(g, '?')
        out.append(s.rstrip())
    return out
want = {   # :h number_relativenumber
    'nu':    ['  1 apple', '  2 pear', '  3 nobody', '  4 there'],
    'nurnu': ['  2 apple', '  1 pear', '3   nobody', '  1 there'],
    'rnu':   ['  2 apple', '  1 pear', '  0 nobody', '  1 there'],
    'none':  ['apple', 'pear', 'nobody', 'there'],
    'long':  ['  2 apple', '  1 pear', '3   nobody', '  1 there'],
}
for n, w in want.items():
    got = rows(n)
    check(got == w, '%-6s %r' % (n, got))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "neosim-number-test: PASS" || echo "neosim-number-test: FAIL"
exit "$fail"

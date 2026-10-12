#!/usr/bin/env bash
# neosim-reglist-test.sh - :reg (v0.60.240): after "ayy j"byw jdd, the page
# read back against the font, row for row as neovim 0.12.5's :registers
# prints it for the same keys (its "+ clipboard and "% rows aside: neosim
# has neither); :q back to the file.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimreg.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'one soup\ntwo salt\nthree pepper\nfour\n' > "$WORK/r.txt"; mcopy -o -i "$WORK/disk.img" "$WORK/r.txt" ::R.TXT
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /R.TXT &" "WAIT:1" "countertop" "WAIT:4"
      'TYPE:"ayyj"bywjdd' "WAIT:1" "TYPE::reg" "KEY:ret" "UNTIL:[neosim] registers" "WAIT:1" "MON:screendump $WORK/reg.ppm" "WAIT:1"
      "TYPE::q" "KEY:ret" "WAIT:1" "MON:screendump $WORK/back.ppm" "WAIT:1")
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
    d = open(f'{work}/{n}.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
lum = lambda c: (c >> 16) + (c >> 8 & 255) + (c & 255)
def row(at, r, ink):
    s = ''
    for c in range(80):
        g = bytes(sum(1 << (7 - i) for i in range(8) if ink(at(241 + c*8 + i, 93 + r*16 + j))) for j in range(14))
        s += shape.get(g, '?')
    return s.rstrip()
at = ppm('reg')
got = [row(at, r, lambda c: lum(c) > 380) for r in range(5)]
want = ['Type Name Content', '  l  ""   three pepper^J', '  l  "1   three pepper^J', '  l  "a   one soup^J', '  c  "b   two']   # neovim's, rstripped
for g, w in zip(got, want): check(g == w, '%-28r as neovim: %r' % (g, w))
check(row(at, 5, lambda c: lum(c) > 380) == '', 'nothing more (no "0: a yank to a named register leaves it, as neovim)')
check('registers' in row(at, 28, lambda c: lum(c) < 240), 'the status line says registers: %r' % row(at, 28, lambda c: lum(c) < 240))
check(row(ppm('back'), 0, lambda c: lum(c) > 380).startswith('  1 one soup'), ':q back to the file')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "neosim-reglist-test: PASS" || echo "neosim-reglist-test: FAIL"
exit "$fail"

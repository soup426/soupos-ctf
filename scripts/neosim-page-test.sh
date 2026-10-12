#!/usr/bin/env bash
# neosim-page-test.sh - paging (v0.60.223): Ctrl-D Ctrl-U (half a screen, 14
# lines, a count kept as the amount), Ctrl-F Ctrl-B (a screen less two),
# zz zt zb, on a 200-line file. Each case from gg; its screen read back
# against the font: the first line number shown and the status line's
# line. The numbers are neovim 0.12.5's (line('w0') and line('.'), set
# lines=30 laststatus=2 scrolloff=0 nostartofline: 28 text rows, as here).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimpage.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
seq -f 'line %g' 200 > "$WORK/p.txt"; mcopy -o -i "$WORK/disk.img" "$WORK/p.txt" ::P.TXT
# keys (^X a Ctrl key), neovim's top line, neovim's cursor line
CASES='^D 15 15
^D^D 29 29
G^U 159 186
^F 27 27
^F^F 53 53
G^B 147 174
G^B^B 121 148
50Gzz 37 50
50Gzt 50 50
50Gzb 23 50
zz 1 1
5Gzb 1 5
190Gzt 190 190
G^D 173 200
^U 1 1
^B 1 1
G^F 200 200
160Gzt^D 173 174
180Gzt^D 180 194
195Gzt^D^D 195 200
180Gzt^F^F 200 200
50Gzt^U 36 36
20G^U 1 6
10Gzt^B 1 28
40Gzt^B 14 41
50Gzb^F 49 49
^Dj^U 1 2
3^D 4 4
3^D^D 7 7'
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /P.TXT &" "WAIT:1" "countertop" "WAIT:4")
n=0
while read -r k _ _; do
    keys+=("TYPE:gg")
    rest=$k
    while [ -n "$rest" ]; do
        if [ "${rest:0:1}" = "^" ]; then keys+=("KEY:ctrl-$(printf '%s' "${rest:1:1}" | tr 'A-Z' 'a-z')"); rest=${rest:2}
        else plain=${rest%%^*}; keys+=("TYPE:$plain"); rest=${rest:${#plain}}; fi
    done
    keys+=("WAIT:1" "MON:screendump $WORK/c$n.ppm" "WAIT:1"); n=$((n + 1))
done <<< "$CASES"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
printf '%s\n' "$CASES" > "$WORK/cases"
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import os, re, sys
work, fonth = sys.argv[1], sys.argv[2]
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
bad = []
cases = [l.split() for l in open(f'{work}/cases').read().splitlines() if l.strip()]
for i, (k, top, cur) in enumerate(cases):
    if not os.path.exists(f'{work}/c{i}.ppm'): bad.append((k, 'no screen')); continue
    at = ppm(f'c{i}')
    first = row(at, 0, lambda c: lum(c) > 380).split()
    st = re.search(r'(\d+):\d+$', row(at, 28, lambda c: lum(c) < 240))
    got = (int(first[0]) if first and first[0].isdigit() else None, int(st.group(1)) if st else None)
    if got != (int(top), int(cur)): bad.append((k, 'neovim %s %s, neosim %s %s' % (top, cur, got[0], got[1])))
for b in bad: print('  FAIL  %s: %s' % b)
print(('  ok    ' if not bad else '  FAIL  ') + '%d of %d cases as neovim has them' % (len(cases) - len(bad), len(cases)))
sys.exit(1 if bad else 0)
PY
[ "$fail" = 0 ] && echo "neosim-page-test: PASS" || echo "neosim-page-test: FAIL"
exit "$fail"

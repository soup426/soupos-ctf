#!/usr/bin/env bash
# neosim-colour-test.sh - neosim's syntax colours (v0.60.217), by the file's
# name: .c (keywords, strings and characters, numbers, // and /* */
# comments, # lines) and .sc (soupyc: its keywords, strings, numbers, #
# comments). Each file opened in turn (:e), screendumped; each token's cells
# have their class's colour (the commonest glyph colour in the cell's top 14
# rows, so the cursor's underline is not counted). A .txt stays plain.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimcol.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf '%s\n' '#include "x.h"' 'int main(void) { /* hi */' '    return 42; // done' '}' 'char *s = "soup";' > "$WORK/t.c"
printf '%s\n' '# a comment' 'fn f(n) { return n + 1 }' 'pour "soup"' > "$WORK/t.sc"
printf '%s\n' 'int return 42 "soup" # not code' > "$WORK/t.txt"
for f in t.c t.sc t.txt; do mcopy -o -i "$WORK/disk.img" "$WORK/$f" "::${f^^}"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /T.C &" "WAIT:1" "countertop" "WAIT:4"
      "MON:screendump $WORK/c.ppm" "WAIT:1" "TYPE::e /T.SC" "KEY:ret" "WAIT:1" "MON:screendump $WORK/sc.ppm" "WAIT:1"
      "TYPE::e /T.TXT" "KEY:ret" "WAIT:1" "MON:screendump $WORK/txt.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys, collections
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
PAGE = 0x1A1B26
KEYWORD, STRING, NUMBER, COMMENT, PREPROC, INK = 0xBB9AF7, 0x9ECE6A, 0xFF9E64, 0x565F89, 0x7DCFFF, 0xC0CAF5
def colour(at, col, row):                 # the text's column col (after the 4 of the number), row row
    c = collections.Counter(at(241 + (4 + col) * 8 + i, 93 + row * 16 + j) for i in range(8) for j in range(14))
    c.pop(PAGE, None)
    return c.most_common(1)[0][0] if c else None
def span(at, row, col, n, want, what):
    got = {colour(at, col + k, row) for k in range(n)} - {None}   # spaces have no glyph
    check(got == {want}, '%s: %s' % (what, ', '.join('%06x' % g for g in got if g is not None)))
c = ppm('c')
span(c, 0, 0, 8, PREPROC, '.c: #include as a # line')
span(c, 1, 0, 3, KEYWORD, '.c: int, a keyword')
span(c, 1, 4, 4, INK, '.c: main, plain')
span(c, 1, 17, 8, COMMENT, '.c: /* hi */, a comment')
span(c, 2, 11, 2, NUMBER, '.c: 42, a number')
span(c, 2, 15, 7, COMMENT, '.c: // done, a comment')
span(c, 4, 10, 6, STRING, '.c: "soup", a string')
s = ppm('sc')
span(s, 0, 0, 11, COMMENT, '.sc: # a comment')
span(s, 1, 0, 2, KEYWORD, '.sc: fn, a keyword')
span(s, 1, 10, 6, KEYWORD, '.sc: return, a keyword')
span(s, 1, 21, 1, NUMBER, '.sc: 1, a number')
span(s, 2, 0, 4, KEYWORD, '.sc: pour, a keyword')
span(s, 2, 5, 6, STRING, '.sc: "soup", a string')
t = ppm('txt')
span(t, 0, 0, 30, INK, '.txt: all plain')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "neosim-colour-test: PASS" || echo "neosim-colour-test: FAIL"
exit "$fail"

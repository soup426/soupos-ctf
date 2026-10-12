#!/usr/bin/env bash
# neosim-find-test.sh - neosim's search (/ ? n N), substitute (:s :%s, plain
# text), visual mode (v V with d y c > <) and . (v0.60.206), each case against
# neovim's (v0.12.5, nvim --clean --headless with sw=4 et: neosim shifts by
# four spaces,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimfind.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=30
printf '%s' $'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:/two' 'KEY:ret' 'TYPE:x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:/two' 'KEY:ret' 'TYPE:nx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:G?two' 'KEY:ret' 'TYPE:x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:/two' 'KEY:ret' 'TYPE:Nx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:/zzz' 'KEY:ret' 'TYPE:x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE::s/o/0/' 'KEY:ret' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE::s/o/0/g' 'KEY:ret' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE::%s/e/E/g' 'KEY:ret' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE::%s/two/2/' 'KEY:ret' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:vlld' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:Vjd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:wveyP' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:Vyjp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" 'TYPE:vjd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:wvecTWO' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:>>' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:Vj>' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" 'TYPE:>>.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:dw.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" 'TYPE:x..' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" 'TYPE:Ahey' 'KEY:esc' 'TYPE:j.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" 'TYPE:cwX' 'KEY:esc' 'TYPE:w.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" 'TYPE:ddj.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" 'TYPE::%s/o/0/g' 'KEY:ret' 'TYPE:u' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" 'TYPE:/o' 'KEY:ret' 'TYPE:nnx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" 'TYPE:vd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" 'TYPE:Vd.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" 'TYPE:3>>' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" 'TYPE:jVj>j.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" 'TYPE:/e' 'KEY:ret' 'TYPE:x' 'TYPE:n.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::q!" "KEY:ret" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for i in $(seq 0 $((N - 1))); do mcopy -n -i "$WORK/disk.img" "::C$i.TXT" "$WORK/got$i" 2>/dev/null; done
python3 - "$WORK" <<'CASES' || fail=1
import sys, os
work = sys.argv[1]
CASES = [['/two', 'CR', 'x'], ['/two', 'CR', 'nx'], ['G?two', 'CR', 'x'], ['/two', 'CR', 'Nx'], ['/zzz', 'CR', 'x'], [':s/o/0/', 'CR'], [':s/o/0/g', 'CR'], [':%s/e/E/g', 'CR'], [':%s/two/2/', 'CR'], ['vlld'], ['Vjd'], ['wveyP'], ['Vyjp'], ['vjd'], ['wvecTWO', 'ESC'], ['>>'], ['Vj>'], ['>>.'], ['dw.'], ['x..'], ['Ahey', 'ESC', 'j.'], ['cwX', 'ESC', 'w.'], ['ddj.'], [':%s/o/0/g', 'CR', 'u'], ['/o', 'CR', 'nnx'], ['vd'], ['Vd.'], ['3>>'], ['jVj>j.'], ['/e', 'CR', 'x', 'n.']]
EXPECTED = ['one wo three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten wo\n', 'one wo three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten wo\n', 'ne two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', '0ne two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', '0ne tw0 three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'onE two thrEE\nfour fivE six\nx = foo.bar(1);\n\nsEvEn  Eight ninE\ntEn two\n', 'one 2 three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten 2\n', ' two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'x = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one twotwo three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two three\nfour five six\none two three\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'our five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one TWO three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', '    one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', '    one two three\n    four five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', '        one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', ' two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two threehey\nfour five sixhey\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'X X three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'four five six\n\nseven  eight nine\nten two\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two three\nfour five six\nx = fo.bar(1);\n\nseven  eight nine\nten two\n', 'ne two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n', 'x = foo.bar(1);\n\nseven  eight nine\nten two\n', '    one two three\n    four five six\n    x = foo.bar(1);\n\nseven  eight nine\nten two\n', 'one two three\n    four five six\n        x = foo.bar(1);\n\nseven  eight nine\nten two\n', 'on two thre\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten two\n']
bad = 0
for i, (c, want) in enumerate(zip(CASES, EXPECTED)):
    p = f'{work}/got{i}'
    got = open(p).read() if os.path.exists(p) else None
    keys = ''.join('<Esc>' if t == 'ESC' else '<CR>' if t == 'CR' else t for t in c)
    if got != want:
        bad += 1
        print('  FAIL  %-16s got %r, nvim %r' % (keys, got, want))
print(('  ok    ' if not bad else '  FAIL  ') + '%d of %d cases as neovim has them' % (len(CASES) - bad, len(CASES)))
sys.exit(1 if bad else 0)
CASES
[ "$fail" = 0 ] && echo "neosim-find-test: PASS" || echo "neosim-find-test: FAIL"
exit "$fail"

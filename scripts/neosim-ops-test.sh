#!/usr/bin/env bash
# neosim-ops-test.sh - neosim's counts, motions, operators, registers, undo
# (v0.60.205), each case against neovim's (v0.12.5, nvim --clean --headless,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimops.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=33
printf '%s' $'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:wdw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:wd$' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:2dd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:3x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:cwHELLO' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:yyjp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:jyyP' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE:ddp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE:xu' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:ddu' 'KEY:ctrl-r' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:J' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:3J' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gdd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjwwdw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:jj$x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:ex' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjjjwx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" 'TYPE:ccnew' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:wwdb' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" 'TYPE:Afoo' 'KEY:esc' 'TYPE:Abar' 'KEY:esc' 'TYPE:u' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" 'TYPE:dj' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" 'TYPE:jdk' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" 'TYPE:ywP' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" 'TYPE:xp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" 'TYPE:wCX' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" 'TYPE:4Gdd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" 'TYPE:2jJ' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gdw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" 'TYPE:G5x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" 'TYPE:ww3lx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C30.TXT" "KEY:ret" "WAIT:1" 'TYPE:ddddu' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C31.TXT" "KEY:ret" "WAIT:1" 'TYPE:dddd2u' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C32.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjwwwwyePjX' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['wdw'], ['wd$'], ['2dd'], ['3x'], ['cwHELLO', 'ESC'], ['yyjp'], ['jyyP'], ['ddp'], ['xu'], ['ddu', 'CR'], ['J'], ['3J'], ['Gdd'], ['jjwwdw'], ['jj$x'], ['ex'], ['jjjjwx'], ['ccnew', 'ESC'], ['wwdb'], ['Afoo', 'ESC', 'Abar', 'ESC', 'u'], ['dj'], ['jdk'], ['ywP'], ['xp'], ['wCX', 'ESC'], ['4Gdd'], ['2jJ'], ['Gdw'], ['G5x'], ['ww3lx'], ['ddddu'], ['dddd2u'], ['jjwwwwyePjX']]
EXPECTED = ['one three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one \nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'x = foo.bar(1);\n\nseven  eight nine\nten\n', ' two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'HELLO two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\none two three\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'four five six\none two three\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'four five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three four five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three four five six x = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\n', 'one two three\nfour five six\nx = .bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1)\n\nseven  eight nine\nten\n', 'on two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  ight nine\nten\n', 'new\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two threefoo\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'x = foo.bar(1);\n\nseven  eight nine\nten\n', 'x = foo.bar(1);\n\nseven  eight nine\nten\n', 'one one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'noe two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one X\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\n\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\n\n', 'one two thre\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'four five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.bar(1);\n\nseven  eight nine\nten\n', 'one two three\nfour five six\nx = foo.barbar(1);\n\nseven  eight nine\nten\n']
bad = 0
for i, (c, want) in enumerate(zip(CASES, EXPECTED)):
    p = f'{work}/got{i}'
    got = open(p).read() if os.path.exists(p) else None
    keys = ''.join('<Esc>' if t == 'ESC' else '<C-r>' if t == 'CR' else t for t in c)
    if got != want:
        bad += 1
        print('  FAIL  %-16s got %r, nvim %r' % (keys, got, want))
print(('  ok    ' if not bad else '  FAIL  ') + '%d of %d cases as neovim has them' % (len(CASES) - bad, len(CASES)))
sys.exit(1 if bad else 0)
CASES
[ "$fail" = 0 ] && echo "neosim-ops-test: PASS" || echo "neosim-ops-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# neosim-chars-test.sh - neosim's f t F T ; , (motions too), r and ~, with
# counts and . (v0.60.222), each case against neovim's (v0.12.5, nvim --clean
# --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimchars.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=36
printf '%s' $'let soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:fpx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:2fpx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:tpx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:$Fsx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:$Tsx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:fp;x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:fp;,x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE:tp;x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE:dfp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:dtp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:cf(q' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:d2fp' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:$dFs' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" 'TYPE:fzx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:dfz' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:rq' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:3rq' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" 'TYPE:40rq' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:$rq' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" 'TYPE:jrQ' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" 'TYPE:~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" 'TYPE:5~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" 'TYPE:j$~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" 'TYPE:j~~~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" 'TYPE:40~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjrq' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" 'TYPE:fp;;;x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" 'TYPE:F(x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" 'TYPE:2tpx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" 'TYPE:t(;x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C30.TXT" "KEY:ret" "WAIT:1" 'TYPE:j3~x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C31.TXT" "KEY:ret" "WAIT:1" 'TYPE:r' 'KEY:esc' 'TYPE:x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C32.TXT" "KEY:ret" "WAIT:1" 'TYPE:fprqfp.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C33.TXT" "KEY:ret" "WAIT:1" 'TYPE:3rz.' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C34.TXT" "KEY:ret" "WAIT:1" 'TYPE:~..' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C35.TXT" "KEY:ret" "WAIT:1" 'TYPE:y2fpP' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['fpx'], ['2fpx'], ['tpx'], ['$Fsx'], ['$Tsx'], ['fp;x'], ['fp;,x'], ['tp;x'], ['dfp'], ['dtp'], ['cf(q', 'ESC'], ['d2fp'], ['$dFs'], ['fzx'], ['dfz'], ['rq'], ['3rq'], ['40rq'], ['$rq'], ['jrQ'], ['~'], ['5~'], ['j$~'], ['j~~~'], ['40~'], ['jjrq'], ['fp;;;x'], ['F(x'], ['2tpx'], ['t(;x'], ['j3~x'], ['r', 'ESC', 'x'], ['fprqfp.'], ['3rz.'], ['~..'], ['y2fpP']]
EXPECTED = ['let sou = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = our(salt, pepper);\nHello World, a soup of soups\nx\n', 'let sop = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(alt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(slt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = our(salt, pepper);\nHello World, a soup of soups\nx\n', 'let sou = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup =pour(salt, pepper);\nHello World, a soup of soups\nx\n', ' = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'p = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'qsalt, pepper);\nHello World, a soup of soups\nx\n', 'our(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(;\nHello World, a soup of soups\nx\n', 'et soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'qet soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'qqq soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper)q\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nQello World, a soup of soups\nx\n', 'Let soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'LET Soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nHello World, a soup of soupS\nx\n', 'let soup = pour(salt, pepper);\nhELlo World, a soup of soups\nx\n', 'LET SOUP = POUR(SALT, PEPPER);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nHello World, a soup of soups\nq\n', 'let soup = pour(salt, peper);\nHello World, a soup of soups\nx\n', 'et soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup =pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pou(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = pour(salt, pepper);\nhELo World, a soup of soups\nx\n', 'et soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let souq = qour(salt, pepper);\nHello World, a soup of soups\nx\n', 'zzzzzoup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'LET soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n', 'let soup = plet soup = pour(salt, pepper);\nHello World, a soup of soups\nx\n']
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
[ "$fail" = 0 ] && echo "neosim-chars-test: PASS" || echo "neosim-chars-test: FAIL"
exit "$fail"

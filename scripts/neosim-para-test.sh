#!/usr/bin/env bash
# neosim-para-test.sh - neosim's paragraphs: { } (counts, the end of the
# text, as motions with the exclusive-linewise rule), ip ap (blank runs,
# counts, visual) (v0.60.235), each case against neovim's (v0.12.5, nvim
# --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimpara.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=32
printf '%s' $'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:}x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:}}x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:2}x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:}}}x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:G{x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:G{{x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:G2{x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjj{x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE:d}' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjjd}' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjjd{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gd{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:y}P' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" TYPE:dip "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" TYPE:dap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" TYPE:jjjdip "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" TYPE:jjjdap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" TYPE:Gdap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" TYPE:Gdip "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" TYPE:jjdip "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" TYPE:jjdap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" TYPE:6Gdip "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" TYPE:6Gdap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" TYPE:d2ap "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" TYPE:vipd "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" TYPE:vapd "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" TYPE:jjjcipnew KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" 'TYPE:}dd' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjj}x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gkk{x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C30.TXT" "KEY:ret" "WAIT:1" 'TYPE:4Gwd}' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C31.TXT" "KEY:ret" "WAIT:1" 'TYPE:4Gwy{P' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['}x'], ['}}x'], ['2}x'], ['}}}x'], ['G{x'], ['G{{x'], ['G2{x'], ['jjj{x'], ['d}'], ['jjjd}'], ['jjjd{'], ['Gd{'], ['y}P'], ['dip'], ['dap'], ['jjjdip'], ['jjjdap'], ['Gdap'], ['Gdip'], ['jjdip'], ['jjdap'], ['6Gdip'], ['6Gdap'], ['d2ap'], ['vipd'], ['vapd'], ['jjjcipnew', 'ESC'], ['}dd'], ['jjj}x'], ['Gkk{x'], ['4Gwd}'], ['4Gwy{P']]
EXPECTED = ['one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree \n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', '\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\n\n\nthree a\nthree b\n', 'one a\none b\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\nthree b\n', 'one a\none b\none a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', '\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'two a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\n\n\nthree a\nthree b\n', 'one a\none b\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\n', 'one a\none b\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\n\nthree a\nthree b\n', 'one a\none b\n\n\n\nthree a\nthree b\n', 'one a\none b\n\nthree a\nthree b\n', 'three a\nthree b\n', '\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'two a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\nnew\n\n\nthree a\nthree b\n', 'one a\none b\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo \n\n\nthree a\nthree b\n', 'one a\none b\n\ntwo \ntwo a\ntwo b\ntwo c\n\n\nthree a\nthree b\n']
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
[ "$fail" = 0 ] && echo "neosim-para-test: PASS" || echo "neosim-para-test: FAIL"
exit "$fail"

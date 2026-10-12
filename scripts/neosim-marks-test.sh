#!/usr/bin/env bash
# neosim-marks-test.sh - neosim's marks: m ' ` as moves and motions, the
# marks moving with lines put in, taken out and joined above them, a mark on
# a line taken out gone, an unset one refused (v0.60.231), each case against
# neovim's (v0.12.5, nvim --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
# (v0.60.238) each case waits for neosim's own "open" and "wrote" lines, not a
# fixed second each: the same keys, the same checks, two seconds a case less.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimmarks.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=20
printf '%s' $'one\n  two two\n    three three\nfour\n  five five\nsix\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" 'TYPE:jjmagg'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" 'TYPE:jjwwmagg`ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" 'TYPE:jjmaggd'"'"'a' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" 'TYPE:jjwmaggd`a' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" TYPE:jjmaggOnew KEY:esc 'TYPE:'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" 'TYPE:jjjmaggdd'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE:jjmaddgg'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" 'TYPE:'"'"'zx' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" 'TYPE:jjmbjjma'"'"'bx`ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" 'TYPE:jjwmaggy`aP' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" 'TYPE:jjmaGd'"'"'a' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" 'TYPE:jjmaG`ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE:jjwmaGd`a' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" TYPE:jjmaggOnew KEY:esc TYPE:Onew KEY:esc 'TYPE:'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" 'TYPE:jjmaggjdd'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE:jjwmaggc`aq' KEY:esc "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" 'TYPE:jjmaggyyP'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" 'TYPE:jjmaggJ'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" 'TYPE:jjjmaggJ'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" TYPE:jjmaGo KEY:esc 'TYPE:'"'"'ax' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
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
CASES = [["jjmagg'ax"], ['jjwwmagg`ax'], ["jjmaggd'a"], ['jjwmaggd`a'], ['jjmaggOnew', 'ESC', "'ax"], ["jjjmaggdd'ax"], ["jjmaddgg'ax"], ["'zx"], ["jjmbjjma'bx`ax"], ['jjwmaggy`aP'], ["jjmaGd'a"], ['jjmaG`ax'], ['jjwmaGd`a'], ['jjmaggOnew', 'ESC', 'Onew', 'ESC', "'ax"], ["jjmaggjdd'ax"], ['jjwmaggc`aq', 'ESC'], ["jjmaggyyP'ax"], ["jjmaggJ'ax"], ["jjjmaggJ'ax"], ['jjmaGo', 'ESC', "'ax"]]
EXPECTED = ['one\n  two two\n    hree three\nfour\n  five five\nsix\n', 'one\n  two two\n    three hree\nfour\n  five five\nsix\n', 'four\n  five five\nsix\n', 'onthree three\nfour\n  five five\nsix\n', 'new\none\n  two two\n    hree three\nfour\n  five five\nsix\n', '  two two\n    three three\nour\n  five five\nsix\n', 'ne\n  two two\nfour\n  five five\nsix\n', 'ne\n  two two\n    three three\nfour\n  five five\nsix\n', 'one\n  two two\n    hree three\nfour\n five five\nsix\n', 'one\n  two two\n    e\n  two two\n    three three\nfour\n  five five\nsix\n', 'one\n  two two\n', 'one\n  two two\n   three three\nfour\n  five five\nsix\n', 'one\n  two two\n    x\n', 'new\nnew\none\n  two two\n    hree three\nfour\n  five five\nsix\n', 'one\n    hree three\nfour\n  five five\nsix\n', 'onqthree three\nfour\n  five five\nsix\n', 'one\none\n  two two\n    hree three\nfour\n  five five\nsix\n', 'one two two\n    hree three\nfour\n  five five\nsix\n', 'one two two\n    three three\nour\n  five five\nsix\n', 'one\n  two two\n    hree three\nfour\n  five five\nsix\n\n']
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
[ "$fail" = 0 ] && echo "neosim-marks-test: PASS" || echo "neosim-marks-test: FAIL"
exit "$fail"

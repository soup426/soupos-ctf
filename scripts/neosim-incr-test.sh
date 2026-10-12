#!/usr/bin/env bash
# neosim-incr-test.sh - neosim's Ctrl-A Ctrl-X: the number under or after the
# cursor, - its sign, 0x hex with its width, counts, ., undo, the cursor to
# the last digit (v0.60.248), each case against neovim's (v0.12.5, nvim
# --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimincr.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=25
printf '%s' $'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" KEY:ctrl-x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" TYPE:5 KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" TYPE:10 KEY:ctrl-x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" TYPE:j KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" TYPE:j KEY:ctrl-x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" TYPE:j4 KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" TYPE:jj KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" TYPE:jj KEY:ctrl-x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" TYPE:jj20 KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" TYPE:jjj KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" TYPE:jjjj KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE:jjjj$' KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" TYPE:5j KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" TYPE:5jw KEY:ctrl-a TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE:5j$' KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" 'TYPE:$' KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" TYPE:w KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" KEY:ctrl-a TYPE:. "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" TYPE:3 KEY:ctrl-a TYPE:. "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
    "TYPE::e! /C20.TXT" "KEY:ret" "UNTIL:[neosim] open /C20.TXT" TYPE:j KEY:ctrl-a KEY:ctrl-a KEY:ctrl-a KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C20.TXT" \
    "TYPE::e! /C21.TXT" "KEY:ret" "UNTIL:[neosim] open /C21.TXT" KEY:ctrl-a TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C21.TXT" \
    "TYPE::e! /C22.TXT" "KEY:ret" "UNTIL:[neosim] open /C22.TXT" TYPE:jj KEY:ctrl-a TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C22.TXT" \
    "TYPE::e! /C23.TXT" "KEY:ret" "UNTIL:[neosim] open /C23.TXT" TYPE:5j KEY:ctrl-x TYPE:u "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C23.TXT" \
    "TYPE::e! /C24.TXT" "KEY:ret" "UNTIL:[neosim] open /C24.TXT" TYPE:j8 KEY:ctrl-a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C24.TXT" \
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
CASES = [['CTRL'], ['CTRL'], ['5', 'CTRL'], ['10', 'CTRL'], ['j', 'CTRL'], ['j', 'CTRL'], ['j4', 'CTRL'], ['jj', 'CTRL'], ['jj', 'CTRL'], ['jj20', 'CTRL'], ['jjj', 'CTRL'], ['jjjj', 'CTRL'], ['jjjj$', 'CTRL'], ['5j', 'CTRL'], ['5jw', 'CTRL', 'x'], ['5j$', 'CTRL'], ['$', 'CTRL'], ['w', 'CTRL'], ['CTRL', '.'], ['3', 'CTRL', '.'], ['j', 'CTRL', 'CTRL', 'CTRL', 'CTRL'], ['CTRL', 'x'], ['jj', 'CTRL', 'x'], ['5j', 'CTRL', 'u'], ['j8', 'CTRL']]
EXPECTED = ['soup 10 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 8 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 14 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup -1 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -2;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -4;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = 1;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x10 end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0e end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x23 end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  43\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  43\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 2000 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 200 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 8\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 10 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 11 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 15 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = 1;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 1 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x1 end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = -3;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n', 'soup 9 pots\nx = 5;\ntemp 0x0f end\nnone here\n  42\nyear 1999 and 7\n']
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
[ "$fail" = 0 ] && echo "neosim-incr-test: PASS" || echo "neosim-incr-test: FAIL"
exit "$fail"

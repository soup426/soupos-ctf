#!/usr/bin/env bash
# neosim-pattern-test.sh - vim's patterns in neosim's / ? n * and :s (. * ^
# $ [..] [^..] \< \> \+ \? \| \( \) \d, a bare + ? ( ) literal; & and \1 in
# the replacement; an empty match) (v0.60.243), each case against neovim's
# (v0.12.5, nvim --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimpat.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=30
printf '%s' $'soup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" TYPE:/s.lt KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" 'TYPE:/[0-9]' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" 'TYPE:/^b' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" 'TYPE:/p$' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" 'TYPE:/\<soup\>' KEY:ret TYPE:nx "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" 'TYPE:/so*u' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE:/a\.b' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" TYPE:/a+b KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" 'TYPE:/a\+b' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" 'TYPE:/\d\d' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" 'TYPE:/(3)' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" 'TYPE:/\(sal\|sto\)' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE:/[^a-z ]' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" 'TYPE:?s.u' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" 'TYPE:G?^b' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE:/oup\>' KEY:ret TYPE:nnx "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" 'TYPE::%s/s\(ou\)p/S\1P/g' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" 'TYPE::%s/[0-9]\+/#/g' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" 'TYPE::s/^/> /' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" 'TYPE::%s/$/;/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
    "TYPE::e! /C20.TXT" "KEY:ret" "UNTIL:[neosim] open /C20.TXT" 'TYPE::%s/o\+/0/g' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C20.TXT" \
    "TYPE::e! /C21.TXT" "KEY:ret" "UNTIL:[neosim] open /C21.TXT" 'TYPE::%s/\<a\>/A/g' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C21.TXT" \
    "TYPE::e! /C22.TXT" "KEY:ret" "UNTIL:[neosim] open /C22.TXT" 'TYPE::%s/s.lt/&&/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C22.TXT" \
    "TYPE::e! /C23.TXT" "KEY:ret" "UNTIL:[neosim] open /C23.TXT" TYPE::%s/a.b/X/g KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C23.TXT" \
    "TYPE::e! /C24.TXT" "KEY:ret" "UNTIL:[neosim] open /C24.TXT" 'TYPE::%s/a\.b/X/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C24.TXT" \
    "TYPE::e! /C25.TXT" "KEY:ret" "UNTIL:[neosim] open /C25.TXT" 'TYPE:jj:s/a?b/Q/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C25.TXT" \
    "TYPE::e! /C26.TXT" "KEY:ret" "UNTIL:[neosim] open /C26.TXT" 'TYPE::%s/\(\w\+\) \(\w\+\)/\2 \1/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C26.TXT" \
    "TYPE::e! /C27.TXT" "KEY:ret" "UNTIL:[neosim] open /C27.TXT" 'TYPE:*x' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C27.TXT" \
    "TYPE::e! /C28.TXT" "KEY:ret" "UNTIL:[neosim] open /C28.TXT" 'TYPE:/b.*h' KEY:ret TYPE:x "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C28.TXT" \
    "TYPE::e! /C29.TXT" "KEY:ret" "UNTIL:[neosim] open /C29.TXT" 'TYPE::%s/x*/-/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C29.TXT" \
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
CASES = [['/s.lt', 'CR', 'x'], ['/[0-9]', 'CR', 'x'], ['/^b', 'CR', 'x'], ['/p$', 'CR', 'x'], ['/\\<soup\\>', 'CR', 'nx'], ['/so*u', 'CR', 'x'], ['/a\\.b', 'CR', 'x'], ['/a+b', 'CR', 'x'], ['/a\\+b', 'CR', 'x'], ['/\\d\\d', 'CR', 'x'], ['/(3)', 'CR', 'x'], ['/\\(sal\\|sto\\)', 'CR', 'x'], ['/[^a-z ]', 'CR', 'x'], ['?s.u', 'CR', 'x'], ['G?^b', 'CR', 'x'], ['/oup\\>', 'CR', 'nnx'], [':%s/s\\(ou\\)p/S\\1P/g', 'CR'], [':%s/[0-9]\\+/#/g', 'CR'], [':s/^/> /', 'CR'], [':%s/$/;/', 'CR'], [':%s/o\\+/0/g', 'CR'], [':%s/\\<a\\>/A/g', 'CR'], [':%s/s.lt/&&/', 'CR'], [':%s/a.b/X/g', 'CR'], [':%s/a\\.b/X/', 'CR'], ['jj:s/a?b/Q/', 'CR'], [':%s/\\(\\w\\+\\) \\(\\w\\+\\)/\\2 \\1/', 'CR'], ['*x'], ['/b.*h', 'CR', 'x'], [':%s/x*/-/', 'CR']]
EXPECTED = ['soup 12 alt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 2 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'oup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'oup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, oups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\n.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\na.b +b a?b\nend of soup 7\n', 'oup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 2 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups 3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 alt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 2 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of oup 7\n', 'soup 12 salt\nroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'sup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'SouP 12 salt\nbroth, SouPs (3) and stock\na.b a+b a?b\nend of SouP 7\n', 'soup # salt\nbroth, soups (#) and stock\na.b a+b a?b\nend of soup #\n', '> soup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt;\nbroth, soups (3) and stock;\na.b a+b a?b;\nend of soup 7;\n', 's0up 12 salt\nbr0th, s0ups (3) and st0ck\na.b a+b a?b\nend 0f s0up 7\n', 'soup 12 salt\nbroth, soups (3) and stock\nA.b A+b A?b\nend of soup 7\n', 'soup 12 saltsalt\nbroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\nX X X\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\nX a+b a?b\nend of soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\na.b a+b Q\nend of soup 7\n', '12 soup salt\nbroth, soups (3) stock and\na.a b+b a?b\nof end soup 7\n', 'soup 12 salt\nbroth, soups (3) and stock\na.b a+b a?b\nend of oup 7\n', 'soup 12 salt\nroth, soups (3) and stock\na.b a+b a?b\nend of soup 7\n', '-soup 12 salt\n-broth, soups (3) and stock\n-a.b a+b a?b\n-end of soup 7\n']
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
[ "$fail" = 0 ] && echo "neosim-pattern-test: PASS" || echo "neosim-pattern-test: FAIL"
exit "$fail"

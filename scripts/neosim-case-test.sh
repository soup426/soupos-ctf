#!/usr/bin/env bash
# neosim-case-test.sh - neosim's gu gU g~ with motions, text objects, counts,
# the doubled forms (guu gugu ...), visual u U ~ gU, and . (v0.60.232), each
# case against neovim's (v0.12.5, nvim --clean --headless with set
# autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimcase.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=34
printf '%s' $'Hot Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" TYPE:gUw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" TYPE:guw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:g~w' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" TYPE:gUe "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:gU$' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:gu$' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" TYPE:2gUw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" TYPE:gU2w "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" TYPE:gUU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" TYPE:guu "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:g~~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" TYPE:2gUU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" TYPE:gUgU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" TYPE:gugu "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:g~g~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:jgUi(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:jgua(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" TYPE:jwwgUiw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" TYPE:gUj "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" TYPE:wgUiwx "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" TYPE:gUwjx "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" TYPE:gUw.jj0. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" 'TYPE:guiwwg~iw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" TYPE:vegU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" TYPE:veu "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" TYPE:veU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" 'TYPE:ve~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" TYPE:Vu "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" TYPE:VjU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" TYPE:jVgU "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C30.TXT" "KEY:ret" "WAIT:1" TYPE:gUiw... "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C31.TXT" "KEY:ret" "WAIT:1" 'TYPE:3g~~' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C32.TXT" "KEY:ret" "WAIT:1" 'TYPE:$gUb' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C33.TXT" "KEY:ret" "WAIT:1" TYPE:jfPguw "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['gUw'], ['guw'], ['g~w'], ['gUe'], ['gU$'], ['gu$'], ['2gUw'], ['gU2w'], ['gUU'], ['guu'], ['g~~'], ['2gUU'], ['gUgU'], ['gugu'], ['g~g~'], ['jgUi('], ['jgua('], ['jwwgUiw'], ['gUj'], ['wgUiwx'], ['gUwjx'], ['gUw.jj0.'], ['guiwwg~iw'], ['vegU'], ['veu'], ['veU'], ['ve~'], ['Vu'], ['VjU'], ['jVgU'], ['gUiw...'], ['3g~~'], ['$gUb'], ['jfPguw']]
EXPECTED = ['HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot soup and cold salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot soup and cold salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hOT sOUP AND cOLD sALAD\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nSALT PEPPER (BASIL) END\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot soup and cold salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hOT sOUP AND cOLD sALAD\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'Hot Soup and Cold Salad\nsalt PEPPER (BASIL) end\nMIXED case Line\nlast\n', 'Hot Soup and Cold Salad\nsalt PEPPER (basil) end\nMIXED case Line\nlast\n', 'Hot Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nSALT PEPPER (BASIL) END\nMIXED case Line\nlast\n', 'Hot OUP and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot sOUP and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hot soup and cold salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'HOT SOUP AND COLD SALAD\nSALT PEPPER (BASIL) END\nMIXED case Line\nlast\n', 'Hot Soup and Cold Salad\nSALT PEPPER (BASIL) END\nMIXED case Line\nlast\n', 'HOT Soup and Cold Salad\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'hOT sOUP AND cOLD sALAD\nSALT pepper (bASIL) END\nmixed CASE lINE\nlast\n', 'Hot Soup and Cold SALAd\nsalt PEPPER (Basil) end\nMIXED case Line\nlast\n', 'Hot Soup and Cold Salad\nsalt pepper (Basil) end\nMIXED case Line\nlast\n']
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
[ "$fail" = 0 ] && echo "neosim-case-test: PASS" || echo "neosim-case-test: FAIL"
exit "$fail"

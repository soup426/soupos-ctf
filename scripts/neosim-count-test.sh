#!/usr/bin/env bash
# neosim-count-test.sh - neosim's counts on i a I A o O (what was typed typed
# again at Esc; o and O a line each), one undo step, . with them, and a count
# before . in place of the change's own (v0.60.234), each case against
# neovim's (v0.12.5, nvim --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimcount.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=20
printf '%s' $'soup\n  pot of broth\nend\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" TYPE:3ihi KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" TYPE:3ahi KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" TYPE:2Ix KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:3A!' KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" TYPE:2onew KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" TYPE:2Onew KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" TYPE:j2onew KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" TYPE:j3Ox KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" TYPE:3ihi KEY:esc TYPE:. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" TYPE:3ihi KEY:esc TYPE:j. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" TYPE:2ia KEY:ret TYPE:b KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" TYPE:3i KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" TYPE:2ia KEY:esc TYPE:x "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" TYPE:5A- KEY:esc TYPE:0x "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" TYPE:3ihi KEY:esc TYPE:u "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" TYPE:2onew KEY:esc TYPE:u "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" TYPE:ihi KEY:esc TYPE:3. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" TYPE:3ihi KEY:esc TYPE:2. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:j2A;' KEY:esc TYPE:k. "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" TYPE:2ihi KEY:esc TYPE:jj0. "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['3ihi', 'ESC'], ['3ahi', 'ESC'], ['2Ix', 'ESC'], ['3A!', 'ESC'], ['2onew', 'ESC'], ['2Onew', 'ESC'], ['j2onew', 'ESC'], ['j3Ox', 'ESC'], ['3ihi', 'ESC', '.'], ['3ihi', 'ESC', 'j.'], ['2ia', 'CR', 'b', 'ESC'], ['3i', 'ESC'], ['2ia', 'ESC', 'x'], ['5A-', 'ESC', '0x'], ['3ihi', 'ESC', 'u'], ['2onew', 'ESC', 'u'], ['ihi', 'ESC', '3.'], ['3ihi', 'ESC', '2.'], ['j2A;', 'ESC', 'k.'], ['2ihi', 'ESC', 'jj0.']]
EXPECTED = ['hihihisoup\n  pot of broth\nend\n', 'shihihioup\n  pot of broth\nend\n', 'xxsoup\n  pot of broth\nend\n', 'soup!!!\n  pot of broth\nend\n', 'soup\nnew\nnew\n  pot of broth\nend\n', 'new\nnew\nsoup\n  pot of broth\nend\n', 'soup\n  pot of broth\n  new\n  new\nend\n', 'soup\n  x\n  x\n  x\n  pot of broth\nend\n', 'hihihhihihiisoup\n  pot of broth\nend\n', 'hihihisoup\n  pothihihi of broth\nend\n', 'a\nba\nbsoup\n  pot of broth\nend\n', 'soup\n  pot of broth\nend\n', 'asoup\n  pot of broth\nend\n', 'oup-----\n  pot of broth\nend\n', 'soup\n  pot of broth\nend\n', 'soup\n  pot of broth\nend\n', 'hhihihiisoup\n  pot of broth\nend\n', 'hihihhihiisoup\n  pot of broth\nend\n', 'soup;;\n  pot of broth;;\nend\n', 'hihisoup\n  pot of broth\nhihiend\n']
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
[ "$fail" = 0 ] && echo "neosim-count-test: PASS" || echo "neosim-count-test: FAIL"
exit "$fail"

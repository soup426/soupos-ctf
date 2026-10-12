#!/usr/bin/env bash
# neosim-star-test.sh - neosim's * and # (whole words, from the word's own
# start, round the end; counts; n N after; a motion's operator) (v0.60.226),
# each case against neovim's (v0.12.5, nvim --clean --headless with set
# autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimstar.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=20
printf '%s' $'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend soup\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:**x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:2*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:*nx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:*Nx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:#x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:##x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE:G#x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE:G#nx' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjw*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjw#x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:j*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:j0*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjfs*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:jj*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:*dw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjw*ndw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" 'TYPE:4j*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:$*x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" 'TYPE:w*x' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['*x'], ['**x'], ['2*x'], ['*nx'], ['*Nx'], ['#x'], ['##x'], ['G#x'], ['G#nx'], ['jjw*x'], ['jjw#x'], ['j*x'], ['j0*x'], ['jjfs*x'], ['jj*x'], ['*dw'], ['jjw*ndw'], ['4j*x'], ['$*x'], ['w*x']]
EXPECTED = ['soup and soups, soupy oup\n  = salt + soup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + oup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + oup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + oup;\nsalt_soup soup salt\nend soup\n', 'oup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend oup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup oup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nnd soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nnd soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend oup\n', 'soup and soups, soupy soup\n  = salt + oup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup alt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup alt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nalt_soup soup salt\nend soup\n', 'soup and soups, soupy \n  = salt + soup;\nsalt_soup soup salt\nend soup\n', 'and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend soup\n', 'soup and soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nnd soup\n', 'soup and soups, soupy soup\n  = salt + oup;\nsalt_soup soup salt\nend soup\n', 'soup nd soups, soupy soup\n  = salt + soup;\nsalt_soup soup salt\nend soup\n']
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
[ "$fail" = 0 ] && echo "neosim-star-test: PASS" || echo "neosim-star-test: FAIL"
exit "$fail"

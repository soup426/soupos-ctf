#!/usr/bin/env bash
# neosim-global-test.sh - neosim's ranges (:2,4d :3d :.,$ :2,3s), :g :g! :v
# with d, s and normal, :sort ! n u, one undo step for :g and :sort
# (v0.60.247), each case against neovim's (v0.12.5, nvim --clean --headless
# with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimglobal.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=23
printf '%s' $'pepper 10\nsalt 2\nsoup 33\nbroth 2\nsalt 2\nbasil 100\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" TYPE::g/salt/d KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" TYPE::v/salt/d KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" 'TYPE::g!/salt/d' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" 'TYPE::g/^s/d' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" 'TYPE::g/2$/s/2/two/' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" 'TYPE::g/salt/normal A!' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE::g/s/normal Ix' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" TYPE::g/zzz/d KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" TYPE::sort KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" 'TYPE::sort!' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" 'TYPE::sort n' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" 'TYPE::sort u' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE::sort! n' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" TYPE::2,4sort KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" TYPE::%sort KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE::.,$sort' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" TYPE::2,4d KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" TYPE::3d KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" TYPE::2,3s/s/S/ KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" TYPE::g/salt/d KEY:ret TYPE:u "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
    "TYPE::e! /C20.TXT" "KEY:ret" "UNTIL:[neosim] open /C20.TXT" TYPE::sort KEY:ret TYPE:u "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C20.TXT" \
    "TYPE::e! /C21.TXT" "KEY:ret" "UNTIL:[neosim] open /C21.TXT" TYPE::g/o/s/o/0/g KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C21.TXT" \
    "TYPE::e! /C22.TXT" "KEY:ret" "UNTIL:[neosim] open /C22.TXT" 'TYPE::v/o/normal dd' KEY:ret "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C22.TXT" \
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
CASES = [[':g/salt/d', 'CR'], [':v/salt/d', 'CR'], [':g!/salt/d', 'CR'], [':g/^s/d', 'CR'], [':g/2$/s/2/two/', 'CR'], [':g/salt/normal A!', 'CR'], [':g/s/normal Ix', 'CR'], [':g/zzz/d', 'CR'], [':sort', 'CR'], [':sort!', 'CR'], [':sort n', 'CR'], [':sort u', 'CR'], [':sort! n', 'CR'], [':2,4sort', 'CR'], [':%sort', 'CR'], [':.,$sort', 'CR'], [':2,4d', 'CR'], [':3d', 'CR'], [':2,3s/s/S/', 'CR'], [':g/salt/d', 'CR', 'u'], [':sort', 'CR', 'u'], [':g/o/s/o/0/g', 'CR'], [':v/o/normal dd', 'CR']]
EXPECTED = ['pepper 10\nsoup 33\nbroth 2\nbasil 100\n', 'salt 2\nsalt 2\n', 'salt 2\nsalt 2\n', 'pepper 10\nbroth 2\nbasil 100\n', 'pepper 10\nsalt two\nsoup 33\nbroth two\nsalt two\nbasil 100\n', 'pepper 10\nsalt 2!\nsoup 33\nbroth 2\nsalt 2!\nbasil 100\n', 'pepper 10\nxsalt 2\nxsoup 33\nbroth 2\nxsalt 2\nxbasil 100\n', 'pepper 10\nsalt 2\nsoup 33\nbroth 2\nsalt 2\nbasil 100\n', 'basil 100\nbroth 2\npepper 10\nsalt 2\nsalt 2\nsoup 33\n', 'soup 33\nsalt 2\nsalt 2\npepper 10\nbroth 2\nbasil 100\n', 'salt 2\nbroth 2\nsalt 2\npepper 10\nsoup 33\nbasil 100\n', 'basil 100\nbroth 2\npepper 10\nsalt 2\nsoup 33\n', 'basil 100\nsoup 33\npepper 10\nsalt 2\nbroth 2\nsalt 2\n', 'pepper 10\nbroth 2\nsalt 2\nsoup 33\nsalt 2\nbasil 100\n', 'basil 100\nbroth 2\npepper 10\nsalt 2\nsalt 2\nsoup 33\n', 'basil 100\nbroth 2\npepper 10\nsalt 2\nsalt 2\nsoup 33\n', 'pepper 10\nsalt 2\nbasil 100\n', 'pepper 10\nsalt 2\nbroth 2\nsalt 2\nbasil 100\n', 'pepper 10\nSalt 2\nSoup 33\nbroth 2\nsalt 2\nbasil 100\n', 'pepper 10\nsalt 2\nsoup 33\nbroth 2\nsalt 2\nbasil 100\n', 'pepper 10\nsalt 2\nsoup 33\nbroth 2\nsalt 2\nbasil 100\n', 'pepper 10\nsalt 2\ns0up 33\nbr0th 2\nsalt 2\nbasil 100\n', 'soup 33\nbroth 2\n']
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
[ "$fail" = 0 ] && echo "neosim-global-test: PASS" || echo "neosim-global-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# neosim-regs-test.sh - neosim's registers: "a-"z (and "A-"Z adding to
# them) with y d c x p P, "" the unnamed, "0 the last yank, "1-"9 the
# deletes of lines moving down (v0.60.240), each case against neovim's
# (v0.12.5, nvim --clean --headless with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimregs.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=22
printf '%s' $'one soup\ntwo salt\nthree pepper\nfour\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" 'TYPE:"ayyjj"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" 'TYPE:"ayw"bywj"ap"bp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" 'TYPE:"ayyj"AyyG"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" 'TYPE:"addG"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" 'TYPE:"add"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" 'TYPE:"addp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE:yyjddk"0p' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" TYPE:yyjddkp "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" 'TYPE:dd"1p' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" 'TYPE:dddd"2p' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" 'TYPE:dddd"1p"2p' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" 'TYPE:"ax$"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE:"adwj"Adw"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" 'TYPE:"ayiwj"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" 'TYPE:"ayy"byyj"bp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE:"ay$G"aP' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" 'TYPE:"add"bddG"ap"bp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" 'TYPE:yy"ayyjdd"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" 'TYPE:"Qyy"qp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" 'TYPE:"adwu"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
    "TYPE::e! /C20.TXT" "KEY:ret" "UNTIL:[neosim] open /C20.TXT" 'TYPE:jyyk"ayy""p' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C20.TXT" \
    "TYPE::e! /C21.TXT" "KEY:ret" "UNTIL:[neosim] open /C21.TXT" 'TYPE:"zyyG"zp"zp' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C21.TXT" \
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
CASES = [['"ayyjj"ap'], ['"ayw"bywj"ap"bp'], ['"ayyj"AyyG"ap'], ['"addG"ap'], ['"add"ap'], ['"addp'], ['yyjddk"0p'], ['yyjddkp'], ['dd"1p'], ['dddd"2p'], ['dddd"1p"2p'], ['"ax$"ap'], ['"adwj"Adw"ap'], ['"ayiwj"ap'], ['"ayy"byyj"bp'], ['"ay$G"aP'], ['"add"bddG"ap"bp'], ['yy"ayyjdd"ap'], ['"Qyy"qp'], ['"adwu"ap'], ['jyyk"ayy""p'], ['"zyyG"zp"zp']]
EXPECTED = ['one soup\ntwo salt\nthree pepper\none soup\nfour\n', 'one soup\ntone one wo salt\nthree pepper\nfour\n', 'one soup\ntwo salt\nthree pepper\nfour\none soup\ntwo salt\n', 'two salt\nthree pepper\nfour\none soup\n', 'two salt\none soup\nthree pepper\nfour\n', 'two salt\none soup\nthree pepper\nfour\n', 'one soup\none soup\nthree pepper\nfour\n', 'one soup\ntwo salt\nthree pepper\nfour\n', 'two salt\none soup\nthree pepper\nfour\n', 'three pepper\none soup\nfour\n', 'three pepper\ntwo salt\none soup\nfour\n', 'ne soupo\ntwo salt\nthree pepper\nfour\n', 'soup\nsone two alt\nthree pepper\nfour\n', 'one soup\ntonewo salt\nthree pepper\nfour\n', 'one soup\ntwo salt\none soup\nthree pepper\nfour\n', 'one soup\ntwo salt\nthree pepper\none soupfour\n', 'three pepper\nfour\none soup\ntwo salt\n', 'one soup\nthree pepper\none soup\nfour\n', 'one soup\none soup\ntwo salt\nthree pepper\nfour\n', 'oone ne soup\ntwo salt\nthree pepper\nfour\n', 'one soup\none soup\ntwo salt\nthree pepper\nfour\n', 'one soup\ntwo salt\nthree pepper\nfour\none soup\none soup\n']
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
[ "$fail" = 0 ] && echo "neosim-regs-test: PASS" || echo "neosim-regs-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# neosim-indent-test.sh - neosim's auto-indent (Enter, o, O keep the line's
# leading spaces; an indent nothing was typed after is taken back at Esc) and %
# (v0.60.218), each case against neovim's (v0.12.5, nvim --clean --headless
# with set autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimindent.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=13
printf '%s' $'fn f(n) {\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" 'TYPE:jox' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjA' 'KEY:ret' 'TYPE:y' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" 'TYPE:jOz' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" 'TYPE:jo' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gow' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" 'TYPE:jj$i' 'KEY:ret' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:%x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" 'TYPE:$%x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjd%' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" 'TYPE:G%x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" 'TYPE:G4l%x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:jj$%x' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjo' 'KEY:ret' 'TYPE:q' 'KEY:esc' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['jox', 'ESC'], ['jjA', 'CR', 'y', 'ESC'], ['jOz', 'ESC'], ['jo', 'ESC'], ['Gow', 'ESC'], ['jj$i', 'CR', 'ESC'], ['%x'], ['$%x'], ['jjd%'], ['G%x'], ['G4l%x'], ['jj$%x'], ['jjo', 'CR', 'q', 'ESC']]
EXPECTED = ['fn f(n) {\n    if n {\n    x\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n        y\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    z\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\nw\n', 'fn f(n) {\n    if n {\n        return (n + 1\n        )\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n {\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n    }\n\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n    }\n}\nx = [1, (2), 3\n', 'fn f(n) {\n    if n {\n        return n + 1)\n    }\n}\nx = [1, (2), 3]\n', 'fn f(n) {\n    if n {\n        return (n + 1)\n\n        q\n    }\n}\nx = [1, (2), 3]\n']
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
[ "$fail" = 0 ] && echo "neosim-indent-test: PASS" || echo "neosim-indent-test: FAIL"
exit "$fail"

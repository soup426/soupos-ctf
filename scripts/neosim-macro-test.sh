#!/usr/bin/env bash
# neosim-macro-test.sh - neosim's macros: q and a letter to record, @ to
# play (counts), @@ again, the macro its register's text ("ap), one undo
# step for a play, . after it the macro's last change (v0.60.241), each case
# against neovim's (v0.12.5, nvim --clean --headless with set autoindent
# expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimmacro.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=18
printf '%s' $'1 soup\n2 salt\n3 pepper\n4 oil\n5 bread\n6 jam\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" 'TYPE:qaA!' KEY:esc TYPE:jqj@a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" 'TYPE:qaA!' KEY:esc TYPE:jq3@a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" TYPE:qaxjq@a@@ "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" TYPE:qaxjq@a2@@ "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" 'TYPE:qbI- ' KEY:esc TYPE:jq4@b "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" TYPE:qaddq@a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE:qadwjq"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" 'TYPE:qaywjq"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" 'TYPE:qcA;' KEY:esc TYPE:jqjj@c "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" 'TYPE:qa~lq@a' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" 'TYPE:qaA ' KEY:esc 'TYPE:q"ap' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" TYPE:qq0xjq5@q "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" TYPE:qaxq@au "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" TYPE:qaxjq2@au "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" TYPE:qaA. KEY:esc TYPE:jqj@a. "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" TYPE:qzyyjpjq@z "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" TYPE:qaqqa TYPE:x TYPE:jq@a "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" TYPE:qaixy KEY:esc TYPE:jq@aj@@ "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
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
CASES = [['qaA!', 'ESC', 'jqj@a'], ['qaA!', 'ESC', 'jq3@a'], ['qaxjq@a@@'], ['qaxjq@a2@@'], ['qbI- ', 'ESC', 'jq4@b'], ['qaddq@a'], ['qadwjq"ap'], ['qaywjq"ap'], ['qcA;', 'ESC', 'jqjj@c'], ['qa~lq@a'], ['qaA ', 'ESC', 'q"ap'], ['qq0xjq5@q'], ['qaxq@au'], ['qaxjq2@au'], ['qaA.', 'ESC', 'jqj@a.'], ['qzyyjpjq@z'], ['qaqqa', 'x', 'jq@a'], ['qaixy', 'ESC', 'jq@aj@@']]
EXPECTED = ['1 soup!\n2 salt\n3 pepper!\n4 oil\n5 bread\n6 jam\n', '1 soup!\n2 salt!\n3 pepper!\n4 oil!\n5 bread\n6 jam\n', ' soup\n salt\n pepper\n4 oil\n5 bread\n6 jam\n', ' soup\n salt\n pepper\n oil\n5 bread\n6 jam\n', '- 1 soup\n- 2 salt\n- 3 pepper\n- 4 oil\n- 5 bread\n6 jam\n', '3 pepper\n4 oil\n5 bread\n6 jam\n', 'soup\n2dwj salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', '1 soup\n2ywj salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', '1 soup;\n2 salt\n3 pepper\n4 oil;\n5 bread\n6 jam\n', '1 Soup\n2 salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', '1 soup A \x1b\n2 salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', ' soup\n salt\n pepper\n oil\n bread\n jam\n', ' soup\n2 salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', ' soup\n2 salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', '1 soup.\n2 salt\n3 pepper.\n4 oil.\n5 bread\n6 jam\n', '1 soup\n2 salt\n1 soup\n3 pepper\n4 oil\n3 pepper\n5 bread\n6 jam\n', ' soup\n salt\n3 pepper\n4 oil\n5 bread\n6 jam\n', 'xy1 soup\n2xy salt\n3 pepper\n4 xyoil\n5 bread\n6 jam\n']
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
[ "$fail" = 0 ] && echo "neosim-macro-test: PASS" || echo "neosim-macro-test: FAIL"
exit "$fail"

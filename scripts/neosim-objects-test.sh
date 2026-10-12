#!/usr/bin/env bash
# neosim-objects-test.sh - neosim's text objects after d c and in visual mode:
# iw aw (counts), i" a" i' a', i( a( ib ab (nested, counts, the next on the
# line), i[ a[, i{ a{ iB (across lines, whole lines inside) (v0.60.227), each
# case against neovim's (v0.12.5, nvim --clean --headless with set
# autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-neosimobj.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
N=45
printf '%s' $'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n' > "$WORK/text"
for i in $(seq 0 $((N - 1))); do mcopy -o -i "$WORK/disk.img" "$WORK/text" "::C$i.TXT"; done
keys=("headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /C0.TXT &" "WAIT:1" "countertop" "WAIT:4" \
    "TYPE::e! /C0.TXT" "KEY:ret" "WAIT:1" TYPE:diw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C1.TXT" "KEY:ret" "WAIT:1" TYPE:daw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C2.TXT" "KEY:ret" "WAIT:1" TYPE:wdiw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C3.TXT" "KEY:ret" "WAIT:1" TYPE:Gwdiw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C4.TXT" "KEY:ret" "WAIT:1" TYPE:Gwdaw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C5.TXT" "KEY:ret" "WAIT:1" TYPE:Gwwdaw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C6.TXT" "KEY:ret" "WAIT:1" 'TYPE:G$daw' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C7.TXT" "KEY:ret" "WAIT:1" TYPE:Gdaw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C8.TXT" "KEY:ret" "WAIT:1" TYPE:G2daw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C9.TXT" "KEY:ret" "WAIT:1" TYPE:Gd2aw "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C10.TXT" "KEY:ret" "WAIT:1" TYPE:ciwz KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C11.TXT" "KEY:ret" "WAIT:1" 'TYPE:jdi"' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C12.TXT" "KEY:ret" "WAIT:1" 'TYPE:jda"' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C13.TXT" "KEY:ret" "WAIT:1" 'TYPE:jf"di"' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C14.TXT" "KEY:ret" "WAIT:1" 'TYPE:jfhdi"' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C15.TXT" "KEY:ret" "WAIT:1" 'TYPE:j$di"' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C16.TXT" "KEY:ret" "WAIT:1" 'TYPE:jdi'"'"'' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C17.TXT" "KEY:ret" "WAIT:1" 'TYPE:jda'"'"'' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C18.TXT" "KEY:ret" "WAIT:1" 'TYPE:jci"q' KEY:esc "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C19.TXT" "KEY:ret" "WAIT:1" 'TYPE:fadi(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C20.TXT" "KEY:ret" "WAIT:1" 'TYPE:fada(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C21.TXT" "KEY:ret" "WAIT:1" 'TYPE:fbdi(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C22.TXT" "KEY:ret" "WAIT:1" 'TYPE:fbda(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C23.TXT" "KEY:ret" "WAIT:1" TYPE:fbdib "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C24.TXT" "KEY:ret" "WAIT:1" TYPE:fbdab "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C25.TXT" "KEY:ret" "WAIT:1" 'TYPE:f(di(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C26.TXT" "KEY:ret" "WAIT:1" 'TYPE:di(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C27.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjfbdi{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C28.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjfbda{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C29.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjf1di[' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C30.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjf1da[' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C31.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjdi{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C32.TXT" "KEY:ret" "WAIT:1" TYPE:jjdiB "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C33.TXT" "KEY:ret" "WAIT:1" 'TYPE:jdi{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C34.TXT" "KEY:ret" "WAIT:1" 'TYPE:jda{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C35.TXT" "KEY:ret" "WAIT:1" 'TYPE:f{di{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C36.TXT" "KEY:ret" "WAIT:1" 'TYPE:Gkdi{' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C37.TXT" "KEY:ret" "WAIT:1" TYPE:fbviwd "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C38.TXT" "KEY:ret" "WAIT:1" 'TYPE:fbvi(d' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C39.TXT" "KEY:ret" "WAIT:1" 'TYPE:fbva(d' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C40.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjfbvi{d' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C41.TXT" "KEY:ret" "WAIT:1" TYPE:ewviwd "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C42.TXT" "KEY:ret" "WAIT:1" 'TYPE:fb2di(' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C43.TXT" "KEY:ret" "WAIT:1" 'TYPE:di)' "TYPE::w" "KEY:ret" "WAIT:1" \
    "TYPE::e! /C44.TXT" "KEY:ret" "WAIT:1" 'TYPE:jjf[di]' "TYPE::w" "KEY:ret" "WAIT:1" \
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
CASES = [['diw'], ['daw'], ['wdiw'], ['Gwdiw'], ['Gwdaw'], ['Gwwdaw'], ['G$daw'], ['Gdaw'], ['G2daw'], ['Gd2aw'], ['ciwz', 'ESC'], ['jdi"'], ['jda"'], ['jf"di"'], ['jfhdi"'], ['j$di"'], ["jdi'"], ["jda'"], ['jci"q', 'ESC'], ['fadi('], ['fada('], ['fbdi('], ['fbda('], ['fbdib'], ['fbdab'], ['f(di('], ['di('], ['jjfbdi{'], ['jjfbda{'], ['jjf1di['], ['jjf1da['], ['jjdi{'], ['jjdiB'], ['jdi{'], ['jda{'], ['f{di{'], ['Gkdi{'], ['fbviwd'], ['fbvi(d'], ['fbva(d'], ['jjfbvi{d'], ['ewviwd'], ['fb2di('], ['di)'], ['jjf[di]']]
EXPECTED = [' f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn (a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend     it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nof   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nit\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nit\n', 'z f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', "fn f(a, (b + c)) {\n    pour + 'x';\n    if [1, 2] { beep }\n}\nend  of   it\n", 'fn f(a, (b + c)) {\n    pour "" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" +;\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "q" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f() {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ()) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ()) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f() {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f() {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] {}\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] \n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if  { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n}\nend  of   it\n', 'fn f(a, (b + c)) \nend  of   it\n', 'fn f(a, (b + c)) {\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n}\nend  of   it\n', 'fn f(a, ( + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ()) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, ) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] {}\n}\nend  of   it\n', 'fn (a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f() {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f() {\n    pour "hot soup" + \'x\';\n    if [1, 2] { beep }\n}\nend  of   it\n', 'fn f(a, (b + c)) {\n    pour "hot soup" + \'x\';\n    if [] { beep }\n}\nend  of   it\n']
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
[ "$fail" = 0 ] && echo "neosim-objects-test: PASS" || echo "neosim-objects-test: FAIL"
exit "$fail"

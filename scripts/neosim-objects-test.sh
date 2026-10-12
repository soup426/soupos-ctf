#!/usr/bin/env bash
# neosim-objects-test.sh - neosim's text objects after d c and in visual mode:
# iw aw (counts), i" a" i' a', i( a( ib ab (nested, counts, the next on the
# line), i[ a[, i{ a{ iB (across lines, whole lines inside) (v0.60.227), each
# case against neovim's (v0.12.5, nvim --clean --headless with set
# autoindent expandtab,
# the keys fed as typed with feedkeys "tx" so undo blocks are as typed; the
# expected files below came from it, so the test does not need nvim). One
# session: for each case, :e! a fresh copy of the same text, the keys, :w.
# (v0.60.238) each case waits for neosim's own "open" and "wrote" lines, not a
# fixed second each: the same keys, the same checks, two seconds a case less.
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
    "TYPE::e! /C0.TXT" "KEY:ret" "UNTIL:[neosim] open /C0.TXT" TYPE:diw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C0.TXT" \
    "TYPE::e! /C1.TXT" "KEY:ret" "UNTIL:[neosim] open /C1.TXT" TYPE:daw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C1.TXT" \
    "TYPE::e! /C2.TXT" "KEY:ret" "UNTIL:[neosim] open /C2.TXT" TYPE:wdiw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C2.TXT" \
    "TYPE::e! /C3.TXT" "KEY:ret" "UNTIL:[neosim] open /C3.TXT" TYPE:Gwdiw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C3.TXT" \
    "TYPE::e! /C4.TXT" "KEY:ret" "UNTIL:[neosim] open /C4.TXT" TYPE:Gwdaw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C4.TXT" \
    "TYPE::e! /C5.TXT" "KEY:ret" "UNTIL:[neosim] open /C5.TXT" TYPE:Gwwdaw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C5.TXT" \
    "TYPE::e! /C6.TXT" "KEY:ret" "UNTIL:[neosim] open /C6.TXT" 'TYPE:G$daw' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C6.TXT" \
    "TYPE::e! /C7.TXT" "KEY:ret" "UNTIL:[neosim] open /C7.TXT" TYPE:Gdaw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C7.TXT" \
    "TYPE::e! /C8.TXT" "KEY:ret" "UNTIL:[neosim] open /C8.TXT" TYPE:G2daw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C8.TXT" \
    "TYPE::e! /C9.TXT" "KEY:ret" "UNTIL:[neosim] open /C9.TXT" TYPE:Gd2aw "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C9.TXT" \
    "TYPE::e! /C10.TXT" "KEY:ret" "UNTIL:[neosim] open /C10.TXT" TYPE:ciwz KEY:esc "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C10.TXT" \
    "TYPE::e! /C11.TXT" "KEY:ret" "UNTIL:[neosim] open /C11.TXT" 'TYPE:jdi"' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C11.TXT" \
    "TYPE::e! /C12.TXT" "KEY:ret" "UNTIL:[neosim] open /C12.TXT" 'TYPE:jda"' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C12.TXT" \
    "TYPE::e! /C13.TXT" "KEY:ret" "UNTIL:[neosim] open /C13.TXT" 'TYPE:jf"di"' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C13.TXT" \
    "TYPE::e! /C14.TXT" "KEY:ret" "UNTIL:[neosim] open /C14.TXT" 'TYPE:jfhdi"' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C14.TXT" \
    "TYPE::e! /C15.TXT" "KEY:ret" "UNTIL:[neosim] open /C15.TXT" 'TYPE:j$di"' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C15.TXT" \
    "TYPE::e! /C16.TXT" "KEY:ret" "UNTIL:[neosim] open /C16.TXT" 'TYPE:jdi'"'"'' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C16.TXT" \
    "TYPE::e! /C17.TXT" "KEY:ret" "UNTIL:[neosim] open /C17.TXT" 'TYPE:jda'"'"'' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C17.TXT" \
    "TYPE::e! /C18.TXT" "KEY:ret" "UNTIL:[neosim] open /C18.TXT" 'TYPE:jci"q' KEY:esc "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C18.TXT" \
    "TYPE::e! /C19.TXT" "KEY:ret" "UNTIL:[neosim] open /C19.TXT" 'TYPE:fadi(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C19.TXT" \
    "TYPE::e! /C20.TXT" "KEY:ret" "UNTIL:[neosim] open /C20.TXT" 'TYPE:fada(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C20.TXT" \
    "TYPE::e! /C21.TXT" "KEY:ret" "UNTIL:[neosim] open /C21.TXT" 'TYPE:fbdi(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C21.TXT" \
    "TYPE::e! /C22.TXT" "KEY:ret" "UNTIL:[neosim] open /C22.TXT" 'TYPE:fbda(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C22.TXT" \
    "TYPE::e! /C23.TXT" "KEY:ret" "UNTIL:[neosim] open /C23.TXT" TYPE:fbdib "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C23.TXT" \
    "TYPE::e! /C24.TXT" "KEY:ret" "UNTIL:[neosim] open /C24.TXT" TYPE:fbdab "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C24.TXT" \
    "TYPE::e! /C25.TXT" "KEY:ret" "UNTIL:[neosim] open /C25.TXT" 'TYPE:f(di(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C25.TXT" \
    "TYPE::e! /C26.TXT" "KEY:ret" "UNTIL:[neosim] open /C26.TXT" 'TYPE:di(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C26.TXT" \
    "TYPE::e! /C27.TXT" "KEY:ret" "UNTIL:[neosim] open /C27.TXT" 'TYPE:jjfbdi{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C27.TXT" \
    "TYPE::e! /C28.TXT" "KEY:ret" "UNTIL:[neosim] open /C28.TXT" 'TYPE:jjfbda{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C28.TXT" \
    "TYPE::e! /C29.TXT" "KEY:ret" "UNTIL:[neosim] open /C29.TXT" 'TYPE:jjf1di[' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C29.TXT" \
    "TYPE::e! /C30.TXT" "KEY:ret" "UNTIL:[neosim] open /C30.TXT" 'TYPE:jjf1da[' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C30.TXT" \
    "TYPE::e! /C31.TXT" "KEY:ret" "UNTIL:[neosim] open /C31.TXT" 'TYPE:jjdi{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C31.TXT" \
    "TYPE::e! /C32.TXT" "KEY:ret" "UNTIL:[neosim] open /C32.TXT" TYPE:jjdiB "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C32.TXT" \
    "TYPE::e! /C33.TXT" "KEY:ret" "UNTIL:[neosim] open /C33.TXT" 'TYPE:jdi{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C33.TXT" \
    "TYPE::e! /C34.TXT" "KEY:ret" "UNTIL:[neosim] open /C34.TXT" 'TYPE:jda{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C34.TXT" \
    "TYPE::e! /C35.TXT" "KEY:ret" "UNTIL:[neosim] open /C35.TXT" 'TYPE:f{di{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C35.TXT" \
    "TYPE::e! /C36.TXT" "KEY:ret" "UNTIL:[neosim] open /C36.TXT" 'TYPE:Gkdi{' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C36.TXT" \
    "TYPE::e! /C37.TXT" "KEY:ret" "UNTIL:[neosim] open /C37.TXT" TYPE:fbviwd "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C37.TXT" \
    "TYPE::e! /C38.TXT" "KEY:ret" "UNTIL:[neosim] open /C38.TXT" 'TYPE:fbvi(d' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C38.TXT" \
    "TYPE::e! /C39.TXT" "KEY:ret" "UNTIL:[neosim] open /C39.TXT" 'TYPE:fbva(d' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C39.TXT" \
    "TYPE::e! /C40.TXT" "KEY:ret" "UNTIL:[neosim] open /C40.TXT" 'TYPE:jjfbvi{d' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C40.TXT" \
    "TYPE::e! /C41.TXT" "KEY:ret" "UNTIL:[neosim] open /C41.TXT" TYPE:ewviwd "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C41.TXT" \
    "TYPE::e! /C42.TXT" "KEY:ret" "UNTIL:[neosim] open /C42.TXT" 'TYPE:fb2di(' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C42.TXT" \
    "TYPE::e! /C43.TXT" "KEY:ret" "UNTIL:[neosim] open /C43.TXT" 'TYPE:di)' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C43.TXT" \
    "TYPE::e! /C44.TXT" "KEY:ret" "UNTIL:[neosim] open /C44.TXT" 'TYPE:jjf[di]' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /C44.TXT" \
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

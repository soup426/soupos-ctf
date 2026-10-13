#!/usr/bin/env bash
# neosim-bytes-test.sh - neosim keeps the bytes it did not change (v0.60.285,
# review R-N4). It made every tab a space and dropped every CR as a file was
# read, so a Makefile saved after any edit lost its tabs, and a CRLF file
# came back LF. Here T1.TXT ("all:", a tab-indented "cc") gets j$x and :w:
# the tab is still there; T2.TXT (CRLF) gets $x and :w: still CRLF.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neobytes.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'all:\n\tcc\n' > "$WORK/t1"; printf 'one\r\ntwo\r\n' > "$WORK/t2"
mcopy -o -i "$WORK/disk.img" "$WORK/t1" ::T1.TXT; mcopy -o -i "$WORK/disk.img" "$WORK/t2" ::T2.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /T1.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" 'TYPE:j$x' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /T1.TXT" \
    "TYPE::e /T2.TXT" "KEY:ret" "UNTIL:[neosim] open /T2.TXT" 'TYPE:$x' "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /T2.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep '\[neosim\]' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
got1=$(mcopy -n -i "$WORK/disk.img" ::T1.TXT - 2>/dev/null | od -An -c | tr -s ' ' | tr '\n' ' ')
want1=$(printf 'all:\n\tc\n' | od -An -c | tr -s ' ' | tr '\n' ' ')
[ "$got1" = "$want1" ] && echo "  ok    a tab-indented line keeps its tab through an edit" || { echo "  FAIL  T1.TXT:$got1(want$want1)"; fail=1; }
got2=$(mcopy -n -i "$WORK/disk.img" ::T2.TXT - 2>/dev/null | od -An -c | tr -s ' ' | tr '\n' ' ')
want2=$(printf 'on\r\ntwo\r\n' | od -An -c | tr -s ' ' | tr '\n' ' ')
[ "$got2" = "$want2" ] && echo "  ok    a CRLF file stays CRLF" || { echo "  FAIL  T2.TXT:$got2(want$want2)"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-bytes-test: PASS" || echo "neosim-bytes-test: FAIL"
exit "$fail"

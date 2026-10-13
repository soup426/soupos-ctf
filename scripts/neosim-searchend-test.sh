#!/usr/bin/env bash
# neosim-searchend-test.sh - a search that matches at a line's end leaves the
# cursor on its last character (v0.60.297, review R-N32). search() put the
# cursor at the match's start unclamped: /$ (or /\>, /x*) left it one past
# the end, and p then wrote a NUL into the file. As nvim: abcdef with
# yl/$<CR>p gives abcdefa; abc / def with /$<CR>nx gives abc / de (n goes on
# to the next line's end, not the same one again).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neosearchend.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'abcdef\nghi\n' > "$WORK/s1"; printf 'abc\ndef\n' > "$WORK/s2"
mcopy -o -i "$WORK/disk.img" "$WORK/s1" ::S1.TXT; mcopy -o -i "$WORK/disk.img" "$WORK/s2" ::S2.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /S1.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" 'TYPE:yl/$' "KEY:ret" "TYPE:p" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /S1.TXT" \
    "TYPE::e /S2.TXT" "KEY:ret" "UNTIL:[neosim] open /S2.TXT" 'TYPE:/$' "KEY:ret" "TYPE:nx" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /S2.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep -E '\[neosim\]|fault' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
g1=$(mcopy -n -i "$WORK/disk.img" ::S1.TXT - 2>/dev/null | od -An -c | tr -s ' ' | tr '\n' ' ')
w1=$(printf 'abcdefa\nghi\n' | od -An -c | tr -s ' ' | tr '\n' ' ')
[ "$g1" = "$w1" ] && echo "  ok    yl/\$<CR>p puts after the last character, as nvim" || { echo "  FAIL  S1.TXT:$g1(want$w1)"; fail=1; }
g2=$(mcopy -n -i "$WORK/disk.img" ::S2.TXT - 2>/dev/null | od -An -c | tr -s ' ' | tr '\n' ' ')
w2=$(printf 'abc\nde\n' | od -An -c | tr -s ' ' | tr '\n' ' ')
[ "$g2" = "$w2" ] && echo "  ok    /\$<CR>n goes on to the next line's end, as nvim" || { echo "  FAIL  S2.TXT:$g2(want$w2)"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-searchend-test: PASS" || echo "neosim-searchend-test: FAIL"
exit "$fail"

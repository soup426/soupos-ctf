#!/usr/bin/env bash
# neosim-longjoin-test.sh - a join past 255 columns is refused, not cut
# (v0.60.286, review R-N5). text_join took only what fitted of the next
# line and threw the rest away: two 250-column lines and J gave one of 255,
# 245 characters gone, and a delete across them or Backspace at a line's
# start dropped text the same way. Here J, a visual delete across both
# lines (200lvj190hd) and Backspace at the start of line 2 are each refused,
# and the file is as it was; a refused c inserts nothing, and :q quits (no
# [+], no undo step: v0.60.295).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neolongjoin.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
python3 -c "
import sys
a = ''.join(chr(97 + i % 26) for i in range(250)); b = ''.join(chr(65 + i % 26) for i in range(250))
open(sys.argv[1], 'w').write(a + '\n' + b + '\n')" "$WORK/long"
mcopy -o -i "$WORK/disk.img" "$WORK/long" ::LONG.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /LONG.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "TYPE:J" "WAIT:1" "TYPE:gg200lvj190hd" "WAIT:1" "KEY:esc" \
    "TYPE:ggj0i" "KEY:backspace" "KEY:esc" "WAIT:1" 'TYPE:gg$vj0c' "KEY:esc" "WAIT:1" \
    "TYPE::q" "KEY:ret" "UNTIL:[neosim] bye" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep '\[neosim\]' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::LONG.TXT "$WORK/after" 2>/dev/null
cmp -s "$WORK/long" "$WORK/after" && echo "  ok    J, a delete across the lines and Backspace at a line's start were refused: the file is as it was (cmp)" \
    || { echo "  FAIL  the file changed: $(wc -c < "$WORK/after" 2>/dev/null) bytes, $(wc -l < "$WORK/after" 2>/dev/null) lines (was 502, 2)"; fail=1; }
[ "$(tr -d '\r' < "$WORK/log" | grep -c '\[neosim\] refused a join past 255')" -ge 4 ] && echo "  ok    each refusal logged" \
    || { echo "  FAIL  refusals logged: $(tr -d '\r' < "$WORK/log" | grep -c '\[neosim\] refused a join past 255') of 4"; fail=1; }
# (v0.60.295, review R-N48) a refusal changes nothing: no undo step, no [+],
# no insert after a refused c: :q quits.
tr -d '\r' < "$WORK/log" | grep -F '[neosim] refused :q' >/dev/null && { echo "  FAIL  :q was refused: a refused join marked the buffer changed"; fail=1; }
tr -d '\r' < "$WORK/log" | grep -F '[neosim] bye' >/dev/null && echo "  ok    :q quit after the refusals: nothing was marked changed" || { echo "  FAIL  neosim did not quit"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-longjoin-test: PASS" || echo "neosim-longjoin-test: FAIL"
exit "$fail"

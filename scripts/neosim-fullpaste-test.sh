#!/usr/bin/env bash
# neosim-fullpaste-test.sh - a linewise paste into a full buffer is refused,
# not a crash (v0.60.288, review R-N2). With 2048 lines (neosim's most) a p
# of a line could insert nothing, but the cursor still went to the line it
# would have made, one past the last, and the next read of it took neosim
# down with its unsaved work. Here a 2048-line file, yyGp, o and O (all
# refused), then :q, which quits: nothing marked changed (v0.60.296).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neofullpaste.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
seq 1001 3048 > "$WORK/full"; mcopy -o -i "$WORK/disk.img" "$WORK/full" ::FULL.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /FULL.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "TYPE:yyGp" "WAIT:1" "TYPE:o" "KEY:esc" "TYPE:O" "KEY:esc" "WAIT:1" \
    "TYPE::q" "KEY:ret" "UNTIL:[neosim] bye" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  neosim did not live to write: $(tr -d '\r' < "$WORK/log" | grep -E '\[neosim\]|fault|killed' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::FULL.TXT "$WORK/after" 2>/dev/null
cmp -s "$WORK/full" "$WORK/after" && echo "  ok    p, o and O into a full buffer were refused; the file is as it was" \
    || { echo "  FAIL  the file changed: $(wc -l < "$WORK/after") lines"; fail=1; }
# (v0.60.296, review R-N49) a refusal changes nothing: no undo step, no [+]: :q quits.
tr -d '\r' < "$WORK/log" | grep -F '[neosim] refused :q' >/dev/null && { echo "  FAIL  :q was refused: a refused p, o or O marked the buffer changed"; fail=1; }
tr -d '\r' < "$WORK/log" | grep -F '[neosim] bye' >/dev/null && echo "  ok    :q quit after them: nothing was marked changed" || { echo "  FAIL  neosim did not quit"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-fullpaste-test: PASS" || echo "neosim-fullpaste-test: FAIL"
exit "$fail"

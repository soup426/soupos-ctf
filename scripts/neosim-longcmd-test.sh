#!/usr/bin/env bash
# neosim-longcmd-test.sh - a long : or / line does not crash neosim
# (v0.60.287, review R-N1). The command line's cursor was drawn at column
# cmdn + 1 with nothing to stop it: 79 characters put it at column 80, past
# the window's pixels, and neosim died of a page fault with its unsaved work.
# Here : and 79 letters, Esc, / and 79 letters, Esc; then x and :w, which a
# living neosim writes.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neolongcmd.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'one\ntwo\n' > "$WORK/a"; mcopy -o -i "$WORK/disk.img" "$WORK/a" ::LC.TXT
L79=$(python3 -c "print('abcdefghij' * 7 + 'abcdefghi')")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /LC.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "TYPE::$L79" "WAIT:1" "KEY:esc" "TYPE:/$L79" "WAIT:1" "KEY:esc" "WAIT:1" \
    "TYPE:x" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /LC.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  neosim did not live to write: $(tr -d '\r' < "$WORK/log" | grep -E '\[neosim\]|fault|killed' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
[ "$(mcopy -n -i "$WORK/disk.img" ::LC.TXT - 2>/dev/null)" = "$(printf 'ne\ntwo')" ] && echo "  ok    79-character : and / lines, then an edit written: neosim lived" \
    || { echo "  FAIL  LC.TXT: $(mcopy -n -i "$WORK/disk.img" ::LC.TXT - 2>/dev/null | head -1)"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-longcmd-test: PASS" || echo "neosim-longcmd-test: FAIL"
exit "$fail"

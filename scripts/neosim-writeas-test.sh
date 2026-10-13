#!/usr/bin/env bash
# neosim-writeas-test.sh - :w OTHER writes a copy and keeps the buffer's name
# (v0.60.278, review R-N3). It took OTHER as the buffer's name and cleared
# [+], as vim does only for a buffer with no name: after x and :w /B.TXT, a
# :q quit with the edit never written to /A.TXT, and a :wq wrote B again.
# Here: /A.TXT opened, x, :w /B.TXT (the copy has the edit), :q refused
# (E37, still changed), :wq writes /A.TXT; both hold the edit, then bye.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neowriteas.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'one\ntwo\n' > "$WORK/a"
mcopy -o -i "$WORK/disk.img" "$WORK/a" ::A.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /A.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "TYPE:x" "TYPE::w /B.TXT" "KEY:ret" "UNTIL:[neosim] wrote /B.TXT" \
    "TYPE::q" "KEY:ret" "WAIT:2" "TYPE::wq" "KEY:ret" "UNTIL:[neosim] bye" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep '\[neosim\]' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep -F '[neosim] refused :q (changed)' "$WORK/l" >/dev/null && echo "  ok    :q after :w /B.TXT is refused: the buffer is still changed" \
    || { echo "  FAIL  :q was not refused"; fail=1; }
grep -F '[neosim] wrote /A.TXT' "$WORK/l" >/dev/null && echo "  ok    :wq wrote /A.TXT, the buffer's own file" \
    || { echo "  FAIL  /A.TXT never written: $(grep -o '\[neosim\] wrote [^ ]*' "$WORK/l" | tr '\n' ';')"; fail=1; }
for f in A B; do
    got=$(mcopy -n -i "$WORK/disk.img" "::/$f.TXT" - 2>/dev/null | od -c | head -2 | tr -s ' ' | tr '\n' ' ')
    [ "$(mcopy -n -i "$WORK/disk.img" "::/$f.TXT" - 2>/dev/null)" = "$(printf 'ne\ntwo')" ] && echo "  ok    /$f.TXT holds the edit" \
        || { echo "  FAIL  /$f.TXT: $got"; fail=1; }
done
[ "$fail" = 0 ] && echo "neosim-writeas-test: PASS" || echo "neosim-writeas-test: FAIL"
exit "$fail"

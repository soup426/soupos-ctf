#!/usr/bin/env bash
# neosim-backref-test.sh - a back-reference in a pattern is refused, not
# taken for a digit (v0.60.318, review R-N58). vim2ere passed \1 through as
# "\1", which rx reads as a plain 1: the duplicate-word idiom
# :%s/\(\w\+\) \1/\1/ left "the the" alone and turned "item 1 here" into
# "item here". rx has no back-references, so \1-\9 in a pattern are refused
# with a message; nothing changes. Here that command on "the the cat" /
# "item 1 here", then :w: the file is as it was and the refusal is logged.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neobackref.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'the the cat\nitem 1 here\n' > "$WORK/b"; mcopy -o -i "$WORK/disk.img" "$WORK/b" ::B.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /B.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" 'TYPE::%s/\(\w\+\) \1/\1/' "KEY:ret" "WAIT:1" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /B.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep '\[neosim\]' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::B.TXT "$WORK/after" 2>/dev/null
cmp -s "$WORK/b" "$WORK/after" && echo "  ok    :%s/\\(\\w\\+\\) \\1/\\1/ changed nothing" \
    || { echo "  FAIL  B.TXT became: $(tr '\n' '/' < "$WORK/after")"; fail=1; }
tr -d '\r' < "$WORK/log" | grep -F '[neosim] refused a back-reference' >/dev/null && echo "  ok    the back-reference was refused, and said" \
    || { echo "  FAIL  no refusal logged"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-backref-test: PASS" || echo "neosim-backref-test: FAIL"
exit "$fail"

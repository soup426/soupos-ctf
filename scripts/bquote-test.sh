#!/usr/bin/env bash
# bquote-test.sh - builtins take quoted arguments (v0.57.4).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bquote.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook call.elf hi > \"/my file.txt\"" "WAIT:2" \
    "pour \"/my file.txt\"" "WAIT:1" \
    "stir \"/two words.txt\" \"some  text\"" "WAIT:1" \
    "decant \"/my file.txt\" \"/copy of.txt\"" "WAIT:1" \
    "relabel '/copy of.txt' '/moved one.txt'" "WAIT:1" \
    "perms \"/moved one.txt\"" "WAIT:1" \
    "mkbowl \"/QBOWL\"" "WAIT:1" "cd '/QBOWL'" "WAIT:1" "pwd" "WAIT:1" "cd /" "WAIT:1" \
    "rmbowl \"/QBOWL\"" "WAIT:1" "strain \"/two words.txt\"" "WAIT:1" \
    "slurp 'two  spaces'" "WAIT:1" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
D=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null)
after() { echo "$L" | grep -A1 -F "> $1" | tail -1; }
[ "$(after 'pour "/my file.txt"')" = "hi" ] && echo "  ok    pour reads a quoted name with a space" || { echo "  FAIL  pour: [$(after 'pour "/my file.txt"')]"; fail=1; }
echo "$L" | grep "Stirred: /two words.txt" >/dev/null \
    && echo "  ok    stir writes a quoted name, its quoted text kept with two spaces" || { echo "  FAIL  stir"; fail=1; }
echo "$D" | grep "::/moved one.txt" >/dev/null && ! echo "$D" | grep "::/copy of.txt" >/dev/null \
    && [ "$(mcopy -n -i "$WORK/disk.img" "::/moved one.txt" - 2>/dev/null)" = "hi" ] \
    && echo "  ok    decant then relabel, quoted with both kinds" || { echo "  FAIL  decant/relabel"; fail=1; }
echo "$L" | grep -A2 -F 'perms "/moved one.txt"' | grep "owner: headchef" >/dev/null \
    && echo "  ok    perms finds a quoted name" || { echo "  FAIL  perms"; fail=1; }
# Bowls named with spaces work since v0.57.6 (lname-test.sh); quoting
# itself is tested here with an 8.3 name.
[ "$(after 'pwd')" = "  /QBOWL" ] && ! echo "$D" | grep "::/QBOWL" >/dev/null \
    && echo "  ok    mkbowl, cd and rmbowl take a quoted bowl" || { echo "  FAIL  bowls: pwd [$(after 'pwd')]"; fail=1; }
! echo "$D" | grep "::/two words.txt" >/dev/null && echo "  ok    strain removes a quoted name" || { echo "  FAIL  strain"; fail=1; }
echo "$L" | grep -x "two  spaces" >/dev/null && echo "  ok    slurp prints its quoted text, spaces kept" || { echo "  FAIL  slurp"; fail=1; }
exit $fail

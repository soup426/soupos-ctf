#!/usr/bin/env bash
# lname-test.sh - bowls get long names, and deletes take theirs away (v0.57.6).
#
# Proven on v0.57.5: `mkbowl "/a bowl"` stored the bowl as A BOWL (a space
# inside an 8.3 name) and cd and rmbowl could not find it; and deleting a
# long-named file left its long-name entries behind, which fsck reported as
# "Orphaned long file name part".
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-lname.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# A lowercase name that fits 8.3, written the way Linux and mtools write it:
# A.TXT plus the case flags in byte 12, no long name (v0.58.2).
echo lc > "$WORK/lc.txt"; mcopy -i "$WORK/disk.img" "$WORK/lc.txt" ::/lc.txt
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "mkbowl \"/a bowl\"" "WAIT:1" "cd \"/a bowl\"" "WAIT:1" "pwd" "WAIT:1" \
    "stir \"inside file.txt\" hi" "WAIT:1" "pour \"/a bowl/inside file.txt\"" "WAIT:1" \
    "strain \"/a bowl/inside file.txt\"" "WAIT:1" "cd /" "WAIT:1" "rmbowl \"/a bowl\"" "WAIT:1" \
    "mkbowl \"/kept long bowl name\"" "WAIT:1" "stir \"/kept long bowl name/a long file inside.txt\" kept" "WAIT:1" \
    "stir \"/a rather long recipe name.txt\" hello" "WAIT:1" "strain \"/a rather long recipe name.txt\"" "WAIT:1" \
    "cook peek.elf / > /LS.OUT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep -A1 "> pwd" | tail -1 | grep -x "  /a bowl" >/dev/null && echo "$L" | grep -A1 -F 'pour "/a bowl/inside file.txt"' | tail -1 | grep -x "hi" >/dev/null \
    && echo "  ok    a bowl named with a space can be entered and written in" || { echo "  FAIL  inside /a bowl"; fail=1; }
D=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null)
! echo "$D" | grep -i "a bowl" >/dev/null && ! echo "$D" | grep -i "a rather long" >/dev/null \
    && echo "  ok    rmbowl and strain remove a long-named bowl and file" || { echo "  FAIL  something was not removed"; fail=1; }
mdir -i "$WORK/disk.img" "::/kept long bowl name" 2>/dev/null | grep "a long file inside.txt" >/dev/null \
    && [ "$(mcopy -n -i "$WORK/disk.img" "::/kept long bowl name/a long file inside.txt" - 2>/dev/null)" = "kept" ] \
    && echo "  ok    mtools finds the long-named bowl and the long-named file in it" || { echo "  FAIL  mtools and the kept bowl"; fail=1; }
out=$(fsck.fat -n "$WORK/disk.img" 2>&1)
! echo "$out" | grep -iE "orphan|bad|wrong|invalid" >/dev/null && [ "$(echo "$out" | wc -l)" -le 2 ] \
    && echo "  ok    fsck is clean: no orphaned long names, no bad 8.3 names" || { echo "  FAIL  fsck:"; echo "$out" | head -6 | sed 's/^/        /'; fail=1; }
mcopy -n -i "$WORK/disk.img" ::/LS.OUT - 2>/dev/null | grep -E "^lc\.txt " >/dev/null \
    && echo "  ok    a lowercase 8.3 name from mtools is shown lowercase (byte 12's case flags)" || { echo "  FAIL  lc.txt shown as: $(mcopy -n -i "$WORK/disk.img" ::/LS.OUT - 2>/dev/null | grep -i lc)"; fail=1; }
exit $fail

#!/usr/bin/env bash
# ctslots-test.sh - window slots do not outlive a desktop (v0.60.265, review
# R-C10). Leave marks a live program's windows gone but kept their slots,
# with their pixels in the kernel's heap, and the next desktop freed only
# the slots of programs that had ended: eight held by one program left
# running shut every later cook out of program windows until a reboot.
# Here saucier's prod.elf hog takes every slot and sleeps; saucier leaves
# (asked twice) and clocks out; the headchef's prod.elf victim must still
# get its window on the next desktop.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-ctslots.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "cook prod.elf hog &" "WAIT:1" "countertop" "UNTIL:prod: hog holds" "WAIT:1" \
    "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 288 12" "WAIT:1" "${CLICK[@]}" "${CLICK[@]}" "WAIT:2" \
    "clockout" "WAIT:2" "headchef" "rosemary" "WAIT:1" "cook prod.elf victim &" "WAIT:1" "countertop" "WAIT:6" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
# Kernel lines land mid-line in a program's output under load ("prod: hog
# holds [countertop] ...\n8"), as prodwin-test found: read the program's
# words with them taken out (v0.60.279; it once said "the hog held none").
python3 -c "import re,sys; print(re.sub(r'\[(countertop|demand|proc \d+|swap|user)\][^\n]*\n', '', open(sys.argv[1], encoding='latin-1').read()), end='')" "$WORK/l" > "$WORK/said"
held=$(grep -oE 'prod: hog holds [0-9]+' "$WORK/said" | grep -oE '[0-9]+$' | head -1)
[ "${held:-0}" -ge 8 ] && echo "  ok    saucier's hog took every slot: $held" || { echo "  FAIL  the hog held ${held:-none}"; fail=1; }
[ "$(grep -c '^\[countertop\] down' "$WORK/l")" -ge 1 ] && echo "  ok    saucier left the desktop" || { echo "  FAIL  saucier's desktop never went down"; fail=1; }
vpid=$(grep -oE '^\[proc [0-9]+\] spawned /prod\.elf' "$WORK/l" | sed -n 2p | grep -oE '[0-9]+')
grep -q "^\[countertop\] pid $vpid asks for a 100x100 window" "$WORK/l" \
    && echo "  ok    the headchef's program got a window on the next desktop" \
    || { echo "  FAIL  the headchef's prod (pid ${vpid:-?}) got no window: the old slots were still held"; fail=1; }
[ "$fail" = 0 ] && echo "ctslots-test: PASS" || echo "ctslots-test: FAIL"
exit "$fail"

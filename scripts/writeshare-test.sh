#!/usr/bin/env bash
# writeshare-test.sh - one cook's writes cannot take everyone's budget
# (v0.60.292, review R-K30). v0.60.283's one budget for all let one cook's
# background writer (it outlives clockout) hold the machine's, and every
# other cook's redirect, soupyc write and neosim or Mincecraft save failed,
# the headchef's too. A cook may hold half of it now, all cooks together
# three quarters; the headchef has the rest. Here saucier's background
# /dev/zero writer is stopped at its share, saucier clocks out, and the
# headchef's small redirect reaches the disk.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-writeshare.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "cook spoon.elf /dev/zero > /home/saucier/z &" "WAIT:10" "clockout" "WAIT:2" \
    "headchef" "rosemary" "WAIT:2" "cook spoon.elf /HELLO.TXT > /h.txt" "UNTIL:/spoon.elf exited with code" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
[ "$(mcopy -n -i "$WORK/disk.img" ::/h.txt - 2>/dev/null)" = "$(mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT - 2>/dev/null)" ] \
    && echo "  ok    with saucier's writer still running, the headchef's redirect reached the disk" \
    || { echo "  FAIL  the headchef's redirect: $(grep -o '/h.txt: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
grep -o "/home/saucier/z: stops at [0-9]* bytes, the cook's share of the write budget" "$WORK/l" >/dev/null \
    && echo "  ok    saucier's writer was stopped at its share: $(grep -o '/home/saucier/z: stops at [0-9]* bytes' "$WORK/l" | head -1)" \
    || { echo "  FAIL  saucier's writer: $(grep -o '/home/saucier/z: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
[ "$fail" = 0 ] && echo "writeshare-test: PASS" || echo "writeshare-test: FAIL"
exit "$fail"

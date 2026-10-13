#!/usr/bin/env bash
# fullbudget-test.sh - on a full disk the shared write budget still holds
# (v0.60.291, review R-K31). wbuf_room 0 stood for "not measured" too, so with
# the disk full the shared cap switched itself off, and each overwrite handle
# could buffer its old file's size again: three overwrites of a 28 MB file
# held 84 MB. Here the headchef fills the disk (a writer killed: its close
# flushes it), then three writers overwrite that file at once: each is
# stopped at 0 bytes by the shared cap, and memory stays free.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-fullbudget.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys=("headchef" "rosemary" "WAIT:1" "cook spoon.elf /dev/zero > /fill &" "WAIT:12" "kill %1" "UNTIL:/spoon.elf exited with code" "WAIT:3"
      "slurp FILLED" "UNTIL:FILLED")
for i in 1 2 3; do keys+=("cook spoon.elf /dev/zero > /fill &" "WAIT:2"); done
keys+=("WAIT:8" "pour HELLO.TXT" "UNTIL:A warm bowl")
timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
after=$(sed -n '/FILLED/,$p' "$WORK/l")
n=$(echo "$after" | grep -c '/fill: stops at 0 bytes, the write buffers together hold the disk.s room')
[ "$n" -ge 3 ] && echo "  ok    the three overwrites of a full disk's file were stopped at 0 bytes by the shared cap" \
    || { echo "  FAIL  the shared cap stopped $n of 3: $(echo "$after" | grep -o '/fill: stops at [0-9]* bytes[^;]*' | tr '\n' ';')"; fail=1; }
echo "$after" | grep -E '/fill: stops at [1-9][0-9]* bytes' >/dev/null && { echo "  FAIL  an overwrite buffered past 0: $(echo "$after" | grep -oE '/fill: stops at [1-9][0-9]* bytes[^;]*' | head -1)"; fail=1; }
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
[ "$fail" = 0 ] && echo "fullbudget-test: PASS" || echo "fullbudget-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# prod-test.sh - pointers a program never had (v0.60.149). prod.elf hands
# SYS_WRITE an address above its break and one between its image and its
# heap, has SYS_READ and SYS_STAT write above its break: each is inside
# the user range, so the old range check let them through and the
# kernel's own copy faulted and panicked. Each must now give -1, the
# program end normally, and the shell go on.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-prod.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1")
for m in write gap read stat; do keys+=("cook prod.elf $m" "UNTIL:headchef@soupOS:/> "); done
keys+=("slurp STILL-HERE" "UNTIL:STILL-HERE")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^prod: ' "$WORK/log" > "$WORK/said"
printf 'prod: write gave -1\nprod: write gave -1\nprod: read gave -1\nprod: stat gave -1\n' | cmp -s - "$WORK/said" \
    || { echo "        prod said: $(tr '\n' ';' < "$WORK/said")"; fail=1; }
[ "$(grep -c '^\[proc [0-9]*\] /prod.elf exited with code 0' "$WORK/log")" = 4 ] || { echo "        not four normal exits"; fail=1; }
grep -i 'panic' "$WORK/log" >/dev/null && { echo "        the kernel panicked"; fail=1; }
grep -x 'STILL-HERE' "$WORK/log" >/dev/null || { echo "        the shell did not go on"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    pointers a program never had: write, the image gap, read and stat each give -1 (they panicked the kernel before v0.60.149)" \
    || echo "  FAIL  bad user pointers"
exit $fail

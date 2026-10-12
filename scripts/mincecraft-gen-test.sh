#!/usr/bin/env bash
# mincecraft-gen-test.sh - Mincecraft's world generator (v0.60.192) against
# scripts/mincecraft_gen.py, the same integer maths in Python: the FNV-1a
# hash of the whole 128x128x64 grid and the spawn column's top, three seeds.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcgen.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
SEEDS="1 42 3000000000"
keys=("headchef" "rosemary" "WAIT:1")
for s in $SEEDS; do keys+=("cook mincecraft.elf -gen -seed $s" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for s in $SEEDS; do
    want=$(python3 scripts/mincecraft_gen.py "$s")
    got=$(command grep -a -o "\[mincecraft\] world seed=$s hash=[0-9]* top=[0-9]*" "$WORK/serial.log" | head -1 | sed 's/\[mincecraft\] world //')
    if [ "$got" = "$want" ]; then echo "  ok    seed $s: $got"; else echo "  FAIL  seed $s: got '$got', want '$want'"; fail=1; fi
done
[ "$fail" = 0 ] && echo "mincecraft-gen-test: PASS" || echo "mincecraft-gen-test: FAIL"
exit "$fail"

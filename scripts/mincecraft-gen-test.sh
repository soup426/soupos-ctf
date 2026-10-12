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
# The terrain itself (v0.60.198): real lakes, a narrow beach, hills. The
# first generator piled heights up between 26 and 36 (2 to 5% under broth).
python3 -I - "$PWD/scripts" <<'TERRAIN' || fail=1
import sys
sys.path.insert(0, sys.argv[1]); import mincecraft_gen as g
ok = True
for seed in (1, 42, 3000000000):
    hs = [g.height(x, z, seed) for x in range(1, 127) for z in range(1, 127)]
    n = len(hs); water = sum(h < g.SEA for h in hs) / n; beach = sum(h == g.SEA for h in hs) / n
    good = water >= 0.08 and beach <= 0.15 and max(hs) >= 40
    print(('  ok    ' if good else '  FAIL  ') + 'seed %d terrain: %.0f%% under broth (8+), %.0f%% beach (15 at most), hills to %d (40+)' % (seed, 100 * water, 100 * beach, max(hs)))
    ok &= good
sys.exit(0 if ok else 1)
TERRAIN
[ "$fail" = 0 ] && echo "mincecraft-gen-test: PASS" || echo "mincecraft-gen-test: FAIL"
exit "$fail"

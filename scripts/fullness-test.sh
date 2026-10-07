#!/usr/bin/env bash
# Does a write that runs out of room halfway give its clusters back?
#
# The repro for the cluster leak fixed in v0.10.3. Not part of smoke-test.sh,
# because it needs a disk of its own: one already nearly full, so a single
# small copy overruns the space that is left. Filling the real 32 MB image
# from inside soupOS works too (soup FILLDISK.SC) but takes minutes, where
# this takes seconds.
#
# Before the fix: 22 KB free, then 0 KB free. Every remaining cluster had been
# allocated to a file whose directory entry never got written, so nothing
# referenced them and nothing ever would. Space gone until a reformat.
set -uo pipefail
cd "$(dirname "$0")/.."

WORK=$(mktemp -d)
LOG=$(mktemp /tmp/soupos-fullness.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-fullness.XXXXXX.sock)
PID=""
cleanup() {
    [ -n "$PID" ] && kill "$PID" 2>/dev/null
    rm -rf "$WORK" "$MON" "$LOG"
}
trap cleanup EXIT

IMG=$WORK/full.img
dd if=/dev/zero of="$IMG" bs=1M count=32 2>/dev/null
mkfs.fat -F 16 -n "SOUPOS" "$IMG" >/dev/null

# 64 KB to copy (32 clusters of 2 KB), with filler leaving far less than that
# free, so the copy has to fail partway through.
dd if=/dev/urandom of="$WORK/big.bin" bs=1K count=64 2>/dev/null
mcopy -i "$IMG" "$WORK/big.bin" ::BIG.BIN
dd if=/dev/zero of="$WORK/filler.bin" bs=1K count=32596 2>/dev/null
mcopy -i "$IMG" "$WORK/filler.bin" ::FILLER.BIN

DISK="$IMG"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$DISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4

python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "larder" "WAIT:2" \
    "decant BIG.BIN COPY.BIN" "WAIT:6" \
    "larder" "WAIT:2" >/dev/null || { echo "FAIL: could not drive QEMU"; exit 1; }

grep -E "KB free, [0-9]+ KB total" "$LOG" | sed 's/^/  /'

# The comma matters: the PMM boot line also says "KB free".
mapfile -t FREE < <(grep -oE "[0-9]+ KB free," "$LOG" | grep -oE "^[0-9]+")
if [ "${#FREE[@]}" -lt 2 ]; then
    echo "  FAIL  could not read free space before and after"; exit 1
fi
if [ "${FREE[0]}" = "${FREE[1]}" ]; then
    echo "  ok    the failed write gave its clusters back (${FREE[0]} KB free, unchanged)"
else
    echo "  FAIL  ${FREE[0]} KB before, ${FREE[1]} KB after: clusters were stranded"
    exit 1
fi

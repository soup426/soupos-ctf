#!/usr/bin/env bash
# reserve-test.sh - one cook cannot fill the disk against the headchef (v0.55.5).
#
# Proven on v0.55.4: with the volume nearly full, the ordinary cook copied
# files into their home until nothing was left; then the headchef's
# `vault 22` failed ("could not read or write the host key on disk") and a
# newly hired cook got no home. Now the last FAT_RESERVE_CLUSTERS (128) are
# kept for the headchef and for the kernel's own writes on anyone's behalf.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-reserve.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# Leave 160 clusters: the cook may use about 30, then meets the reserve.
free=$(fsck.fat -n "$WORK/disk.img" | grep -oE "[0-9]+/[0-9]+ clusters" | awk -F'[/ ]' '{print $2-$1}')
dd if=/dev/zero of="$WORK/filler.bin" bs=2048 count=$((free - 160)) 2>/dev/null
mcopy -i "$WORK/disk.img" "$WORK/filler.bin" ::FILLER.BIN; rm -f "$WORK/filler.bin"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev user,id=n0 -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
args=(); for i in 1 2 3 4 5 6 7 8 9 10; do args+=("decant /BIGSCRIPT.SC /home/cook/B$i.SC" "WAIT:1"); done
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "${args[@]}" "larder" "WAIT:1" "clockout" "WAIT:1" "headchef" "rosemary" "WAIT:2" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "vault 22" "WAIT:3" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
left=$(echo "$L" | grep -oE "^\[larder\] free=[0-9]+" | grep -oE "[0-9]+$")
if [ -n "$left" ] && [ "$left" -ge 124 ] && [ "$left" -le 132 ] && echo "$L" | grep "Write failed" >/dev/null \
   && echo "$L" | grep "^\[fat\] only the reserve is left: refused a cluster to uid 1" >/dev/null; then
    echo "  ok    the cook's writes stop at the reserve ($left clusters left, refusal logged)"
else
    echo "  FAIL  the cook was not stopped at the reserve (left: ${left:-?})"; fail=1
fi
echo "$L" | grep "vault open on port 22" >/dev/null && echo "$L" | grep "new host key written" >/dev/null \
    && echo "  ok    the headchef still opens the vault, writing its host key" || { echo "  FAIL  vault 22"; fail=1; }
echo "$L" | grep "Hired: saucier now has a place in the kitchen, and a home at /home/saucier" >/dev/null \
    && mtype -i "$WORK/disk.img" ::/etc/kitchen | grep "^saucier:" >/dev/null \
    && echo "  ok    and hires a cook, who gets a home, with the roster saved" || { echo "  FAIL  hire after the cook filled the disk"; fail=1; }
[ "$(fsck.fat -n "$WORK/disk.img" 2>&1 | wc -l)" -le 2 ] \
    && echo "  ok    fsck is clean" || { echo "  FAIL  fsck"; fsck.fat -n "$WORK/disk.img" | head; fail=1; }
exit $fail

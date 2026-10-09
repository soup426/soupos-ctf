#!/usr/bin/env bash
# redirect-test.sh - > keeps big output, and says when it cannot (v0.56.4).
#
# Proven on v0.56.3 while measuring rack.elf: a 3 MB result redirected with
# `>` left exactly 2097152 bytes, and 5 and 7 MB results left empty files,
# every time with exit code 0. The redirect kept the whole file in one
# block of the 8 MB kernel heap, and when that stopped growing nobody heard.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-redirect.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
python3 -c "
import random; random.seed(1)
open('$WORK/B.TXT', 'wb').write(bytes(random.getrandbits(8) for _ in range(5 * 1048576)))"
mcopy -i "$WORK/disk.img" "$WORK/B.TXT" ::/B.TXT
# Leave room for one 5 MB copy (and a little), not two.
free=$(fsck.fat -n "$WORK/disk.img" | grep -oE "[0-9]+/[0-9]+ clusters" | awk -F'[/ ]' '{print $2-$1}')
dd if=/dev/zero of="$WORK/filler.bin" bs=2048 count=$((free - 2560 - 1024)) 2>/dev/null
mcopy -i "$WORK/disk.img" "$WORK/filler.bin" ::FILLER.BIN; rm -f "$WORK/filler.bin"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook spoon.elf /B.TXT > /C.TXT" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
for i in $(seq 1 120); do [ "$(grep -acE '^\[proc [0-9]+\] /spoon.elf exited' "$WORK/serial.log")" -ge 1 ] && break; sleep 1; done
python3 scripts/qemu_keys.py "$WORK/mon.sock" "cook spoon.elf /B.TXT > /D.TXT" >/dev/null 2>&1
for i in $(seq 1 120); do [ "$(grep -acE '^\[proc [0-9]+\] /spoon.elf exited' "$WORK/serial.log")" -ge 2 ] && break; sleep 1; done
sleep 1; kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/C.TXT "$WORK/C.TXT" 2>/dev/null
cmp -s "$WORK/B.TXT" "$WORK/C.TXT" \
    && echo "  ok    a 5 MB output through > reaches the disk whole (cmp)" || { echo "  FAIL  the 5 MB copy: $(wc -c < "$WORK/C.TXT" 2>/dev/null) bytes"; fail=1; }
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep "the output to /D.TXT did not reach the disk whole" >/dev/null \
    && echo "$L" | grep -E "^\[proc [0-9]+\] /spoon.elf exited with code 1" >/dev/null \
    && echo "$L" | grep "^\[vfs\] /D.TXT: the write to disk failed" >/dev/null \
    && echo "  ok    one that does not fit is reported on the terminal and in the log, and exits 1" \
    || { echo "  FAIL  the failed output was not reported"; fail=1; }
[ "$(fsck.fat -n "$WORK/disk.img" 2>&1 | wc -l)" -le 2 ] && echo "  ok    fsck is clean after the failed write" \
    || { echo "  FAIL  fsck"; fsck.fat -n "$WORK/disk.img" | head -5; fail=1; }
exit $fail

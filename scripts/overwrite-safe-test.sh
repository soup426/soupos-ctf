#!/usr/bin/env bash
# overwrite-safe-test.sh - an overwrite that fails keeps the old file
# (v0.60.277, review R-M4). fat_write freed the old file's chain before
# writing the new one: a write that then ran out of room had already given
# the old file's clusters away, written part of the new data into them, and
# left the directory entry pointing at a freed chain (a Mincecraft save that
# failed lost the world it was replacing). The new chain is written first
# now and the old freed only once the entry points at the new. Here /X.TXT
# (10 KB) is overwritten by decant with /BIG.TXT (60 KB) on a disk with 20
# KB free: the copy fails, /X.TXT still holds its old bytes, fsck is clean.
set -uo pipefail
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-owsafe.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
python3 -c "
open('$WORK/X.TXT', 'wb').write(b'OLDWORLD' * 1280)
import random; random.seed(7)
open('$WORK/BIG.TXT', 'wb').write(bytes(random.getrandbits(8) for _ in range(61440)))"
mcopy -i "$WORK/disk.img" "$WORK/X.TXT" ::/X.TXT
mcopy -i "$WORK/disk.img" "$WORK/BIG.TXT" ::/BIG.TXT
# Leave ten clusters (20 KB) free: not room for BIG.TXT even with X.TXT's five.
free=$(fsck.fat -n "$WORK/disk.img" | grep -oE "[0-9]+/[0-9]+ clusters" | awk -F'[/ ]' '{print $2-$1}')
dd if=/dev/zero of="$WORK/filler.bin" bs=2048 count=$((free - 10)) 2>/dev/null
mcopy -i "$WORK/disk.img" "$WORK/filler.bin" ::FILLER.BIN; rm -f "$WORK/filler.bin"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 90 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "decant /BIG.TXT /X.TXT" "WAIT:3" "slurp OW-DONE" "UNTIL:OW-DONE" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/X.TXT "$WORK/X.after" 2>/dev/null
cmp -s "$WORK/X.TXT" "$WORK/X.after" && echo "  ok    the failed overwrite left /X.TXT whole (cmp)" \
    || { echo "  FAIL  /X.TXT after the failed overwrite: $(wc -c < "$WORK/X.after" 2>/dev/null) bytes, not its old 10240"; fail=1; }
cmp -s "$WORK/BIG.TXT" "$WORK/X.after" && { echo "  FAIL  the overwrite did not fail (the disk had room?)"; fail=1; }
f=$(fsck.fat -n "$WORK/disk.img" 2>&1)
[ "$(echo "$f" | wc -l)" -le 2 ] && echo "  ok    fsck is clean after it" || { echo "  FAIL  fsck: $(echo "$f" | sed -n 2,4p | tr '\n' ';')"; fail=1; }
[ "$fail" = 0 ] && echo "overwrite-safe-test: PASS" || echo "overwrite-safe-test: FAIL"
exit "$fail"

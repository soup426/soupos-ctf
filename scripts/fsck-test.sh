#!/usr/bin/env bash
# Does the FAT stay consistent after soupOS writes to it?
#
# Every other filesystem test checks that soupOS reads back what it wrote.
# This one hands the image to fsck.fat afterwards, which checks the volume as
# a whole: lost clusters, cross-linked chains, sizes that disagree with their
# chains, the FAT copies agreeing, and names and fields other systems read.
# The first time it ran it found every file soupOS creates flagged as a bad
# name, because the permission byte lived in the field FAT uses for case
# flags (fixed v0.41.0).
#
# The workload touches every write path: login (writes /etc/kitchen), a cook
# hired (rewrites it), scripts that write, read back and delete files with
# long and short names, a bowl made, filled, emptied and removed, a
# permission change, and fatstress's two concurrent writers.
set -uo pipefail
cd "$(dirname "$0")/.."

TESTDISK=$(mktemp /tmp/soupos-fsckdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-fsck.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-fsck.XXXXXX.sock)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
fail=0
pre=$(fsck.fat -n "$TESTDISK" 2>&1)
if [ "$(echo "$pre" | wc -l)" -le 2 ]; then echo "  ok    the image is clean before soupOS touches it"
else echo "  FAIL  the image was already unclean:"; echo "$pre" | sed 's/^/        /' | head -8; fail=1; fi

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" \
    "soup LINES.SC" "WAIT:3" "soup LONGWR.SC" "WAIT:5" "soup LONGSTR.SC" "WAIT:4" \
    "mkbowl TBOWL" "WAIT:1" "stir TBOWL/A.TXT soup" "WAIT:1" "perms TBOWL/A.TXT rw----" "WAIT:1" \
    "strain TBOWL/A.TXT" "WAIT:1" "rmbowl TBOWL" "WAIT:1" \
    "stir KEEP.TXT this one stays" "WAIT:1" "perms KEEP.TXT rw-r--" "WAIT:1" \
    "fatstress" "WAIT:30" >/dev/null 2>&1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

grep -q "\[fatstress\] PASS" "$LOG" && echo "  ok    the workload ran (fatstress passed)" \
    || { echo "  FAIL  the workload did not finish"; fail=1; }
post=$(fsck.fat -n "$TESTDISK" 2>&1)
if [ "$(echo "$post" | wc -l)" -le 2 ]; then
    echo "  ok    fsck.fat finds nothing wrong afterwards ($(echo "$post" | tail -1 | sed 's/^[^:]*: //'))"
else
    echo "  FAIL  fsck.fat found problems after soupOS wrote:"; echo "$post" | sed 's/^/        /' | head -14; fail=1
fi
mtype -i "$TESTDISK" ::KEEP.TXT 2>/dev/null | grep -q "this one stays" \
    && echo "  ok    a file soupOS wrote reads back through mtools" \
    || { echo "  FAIL  mtools could not read KEEP.TXT"; fail=1; }
exit $fail

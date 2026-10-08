#!/usr/bin/env bash
# Does something OTHER than soupOS agree about the long names it wrote?
#
# soupOS reading back its own file only proves it is self-consistent. mtools is
# an independent FAT implementation, so its agreement is real evidence that the
# directory entries are right: the chain order, the checksum binding the chain
# to its alias, the UCS-2 encoding and the ~N tail.
#
# Known and cosmetic: mtools shows the ALIAS in lower case. Byte 12 of a
# directory entry is where VFAT keeps "base is lower case" (0x08) and
# "extension is lower case" (0x10), and soupOS keeps its permission mode
# there - FAT_PERM_OX and FAT_PERM_OW are those same two bits. The long name
# is authoritative and correct; only the alias's display case is affected.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-lfn.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-lfn.XXXXXX.sock)
TESTDISK=$(mktemp /tmp/soupos-lfndisk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "soup LONGWR.SC" "WAIT:5" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
sleep 1; kill "$PID" 2>/dev/null; PID=""

fail=0
grep -qE "read back written by soupOS" "$LOG" \
    && echo "  ok    soupOS read back its own long name" \
    || { echo "  FAIL  soupOS could not read back what it wrote"; fail=1; }

out=$(mdir -i "$TESTDISK" 2>/dev/null)
echo "$out" | grep -iE "my long" | sed 's/^/  /'

for want in "My Long Note.txt" "My Long Novel.txt"; do
    if echo "$out" | grep -F "$want" >/dev/null; then
        echo "  ok    mtools reads '$want'"
    else
        echo "  FAIL  mtools cannot see '$want'"; fail=1
    fi
done

# Two names sharing a stem must not share an alias.
aliases=$(echo "$out" | grep -iE "my long" | awk '{print $1}' | sort -u | wc -l)
if [ "$aliases" -ge 2 ]; then
    echo "  ok    the colliding names got different aliases"
else
    echo "  FAIL  both long names share one alias"; fail=1
fi
exit $fail

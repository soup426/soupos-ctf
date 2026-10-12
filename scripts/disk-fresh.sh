#!/usr/bin/env bash
# disk-fresh.sh [disk] - does the disk image hold the programs just built?
# (v0.60.252, review R-T2). The tests boot copies of disk.img, and make
# never rebuilds it ($(DISK) has no prerequisites), so a change under user/
# was tested only if someone ran rm disk.img && make disk. Every program
# the Makefile's disk recipe copies (mcopy ... user/X.elf ::NAME) is read
# back out of the image and compared with user/X.elf; any that differ, or
# are missing, are named and the exit is 1. The programs must be built
# first (make user).
cd "$(dirname "$0")/.."
DISK=${1:-disk.img}
[ -f "$DISK" ] || { echo "no $DISK: make disk"; exit 1; }
stale=""; n=0
while read -r elf name; do
    [ -f "$elf" ] || { stale="$stale $name(not built)"; continue; }
    n=$((n + 1))
    mcopy -n -i "$DISK" "::$name" - 2>/dev/null | cmp -s - "$elf" || stale="$stale $name"
done < <(sed -nE 's/^\s*mcopy -i \$\(DISK\) -o (user\/[a-z0-9_]+\.elf)\s+::(\S+).*/\1 \2/p' Makefile)
[ "$n" -gt 0 ] || { echo "no programs found in the Makefile's disk recipe"; exit 1; }
if [ -n "$stale" ]; then
    echo "$DISK is older than the programs built:$stale (rm $DISK && make disk)"
    exit 1
fi
echo "$DISK holds the $n programs just built"

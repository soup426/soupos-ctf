#!/usr/bin/env bash
# disk-fresh-test.sh - scripts/disk-fresh.sh (v0.60.252), which the full
# check runs before any test: on a copy of disk.img whose programs match
# the ones just built it passes; with one program inside replaced by
# another it fails and names it. No QEMU.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-diskfresh.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
out=$(bash scripts/disk-fresh.sh "$WORK/disk.img" 2>&1) || { echo "        a fresh disk fails: $out"; fail=1; }
mcopy -i "$WORK/disk.img" -o user/greet.elf ::SPOON.ELF
out=$(bash scripts/disk-fresh.sh "$WORK/disk.img" 2>&1) && { echo "        a stale SPOON.ELF passes"; fail=1; }
case "$out" in *SPOON.ELF*) ;; *) echo "        the stale one is not named: $out"; fail=1 ;; esac
[ "$fail" = 0 ] && echo "disk-fresh-test: PASS" || echo "disk-fresh-test: FAIL"
exit "$fail"

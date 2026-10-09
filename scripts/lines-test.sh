#!/usr/bin/env bash
# Does a file soupyc wrote from another file match, byte for byte, what the
# host computes from the same input?
#
# LINES.SC reads SOUP.TXT with lines(), numbers, upper-cases and reverses it,
# and writes SOUPUP.TXT with write_lines(). soupOS reading it back only proves
# it is self-consistent; this boots a copy of the disk, runs the script, then
# has mtools pull both files out of the image and compares the output with
# the same transform done here in Python.
set -uo pipefail
cd "$(dirname "$0")/.."

TESTDISK=$(mktemp /tmp/soupos-linesdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-lines.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-lines.XXXXXX.sock)
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
sleep 4
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "soup LINES.SC" "WAIT:3" >/dev/null 2>&1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

fail=0
grep -q "wrote 6 lines" "$LOG" && echo "  ok    the script ran and wrote six lines" \
    || { echo "  FAIL  the script did not report writing six lines"; fail=1; }

seed=$(mktemp); got=$(mktemp); want=$(mktemp)
mcopy -i "$TESTDISK" ::SOUP.TXT "$seed" 2>/dev/null
mcopy -i "$TESTDISK" ::SOUPUP.TXT "$got" 2>/dev/null
python3 - "$seed" "$want" <<'PY'
import sys
lines = open(sys.argv[1], "rb").read().decode().split("\n")
if lines and lines[-1] == "": lines.pop()          # the trailing newline, as lines() drops it
out = [f"{i + 1}: {l.upper()}" for i, l in enumerate(lines)][::-1]
open(sys.argv[2], "wb").write(("\n".join(out) + "\n").encode())
PY
if [ -s "$got" ] && cmp -s "$got" "$want"; then
    echo "  ok    SOUPUP.TXT matches the host's transform byte for byte ($(wc -c < "$got") bytes)"
else
    echo "  FAIL  SOUPUP.TXT differs from the host's transform"; diff "$want" "$got" | head -8; fail=1
fi
rm -f "$seed" "$got" "$want"
exit $fail

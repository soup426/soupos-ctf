#!/usr/bin/env bash
# challenge-test.sh - the CHALLENGE build keeps its deliberate weaknesses
# (v0.54.0). check.sh compiled that build but never booted it, so nothing
# noticed if a hardening change reached it. Run in a tree of its own: it
# rebuilds soupOS.iso and disk.img as CHALLENGE=1 DOOM=0.
#
# Stage 2 ("Salt to Taste", docs/challenge-solutions.md) needs an ordinary
# cook to read /etc/kitchen and find unsalted AlphaSOUP-32 hashes.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
make clean >/dev/null 2>&1
make CHALLENGE=1 DOOM=0 >/dev/null 2>&1 || { echo "  FAIL  the CHALLENGE build did not build"; exit 1; }
rm -f disk.img; make CHALLENGE=1 DOOM=0 disk >/dev/null 2>&1 || { echo "  FAIL  the CHALLENGE disk did not build"; exit 1; }
LOG=$(mktemp /tmp/soupos-chal.XXXXXX.log); MON=$(mktemp -u /tmp/soupos-chal.XXXXXX.sock); PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON"; if [ "$fail" != 0 ]; then echo "  serial log kept at $LOG"; else rm -f "$LOG"; fi; }
trap cleanup EXIT
qemu-system-i386 -accel kvm -cpu host -drive "file=disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$MON" "cook" "soup" "WAIT:2" "pour /etc/kitchen" "WAIT:1" "perms /etc/kitchen" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$LOG" | grep -E '^headchef:0:[0-9a-f]{8}$' >/dev/null \
    && echo "  ok    stage 2: an ordinary cook reads the headchef's unsalted AlphaSOUP-32 hash" \
    || { echo "  FAIL  stage 2 is closed: the cook could not read the roster's hashes"; fail=1; }
grep -q "owner: headchef   perms: rwxr-x" "$LOG" \
    && echo "  ok    the roster stays world-readable in this build" || { echo "  FAIL  the roster's mode changed"; fail=1; }
exit $fail

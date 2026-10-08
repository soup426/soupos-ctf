#!/usr/bin/env bash
# May a cook signal only their own processes? (v0.51.0)
#
# The headchef starts hog.elf in the background and clocks out. The ordinary
# cook then tries to kill it by pid and to kill a kernel task (fbcon), and
# both must be refused; the cook's own hog.elf they may kill. The headchef
# comes back and may kill the first hog. The pid is read from the log between
# the two halves of the drive, so the cook types the real one.
set -uo pipefail
cd "$(dirname "$0")/.."
TESTDISK=$(mktemp /tmp/soupos-ownerdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-owner.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-owner.XXXXXX.sock)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT
cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4
fail=0
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "cook hog.elf &" "WAIT:2" >/dev/null \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
chef_pid=$(grep -oE '\[proc [0-9]+\] spawned /hog\.elf' "$LOG" | head -1 | grep -oE '[0-9]+')
fbcon_id=$(sed -n 's/^\[fbcon\].*task \([0-9][0-9]*\).*/\1/p' "$LOG" | head -1); fbcon_id=${fbcon_id:-1}
python3 scripts/qemu_keys.py "$MON" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "kill $chef_pid" "WAIT:2" "kill $fbcon_id" "WAIT:1" \
    "cook hog.elf &" "WAIT:2" "orders" "WAIT:1" "kill %" "WAIT:2" \
    "clockout" "WAIT:1" "headchef" "rosemary" "WAIT:2" "kill $chef_pid" "WAIT:2" >/dev/null \
    || { echo "  FAIL  could not drive QEMU (second half)"; exit 1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

grep -q "cook may not signal $chef_pid" "$LOG" && echo "  ok    a cook cannot kill the headchef's program (pid $chef_pid)" \
    || { echo "  FAIL  the cook was not refused on pid $chef_pid"; fail=1; }
grep -q "cook may not kill task $fbcon_id" "$LOG" && echo "  ok    a cook cannot kill a kernel task" \
    || { echo "  FAIL  the cook was not refused on kernel task $fbcon_id"; fail=1; }
cook_pid=$(grep -oE '\[proc [0-9]+\] spawned /hog\.elf' "$LOG" | sed -n 2p | grep -oE '[0-9]+')
grep -q "\[proc $cook_pid\] /hog.elf exited with code -137" "$LOG" && echo "  ok    a cook may kill their own (pid $cook_pid)" \
    || { echo "  FAIL  the cook's own program was not killed"; fail=1; }
grep -q "\[proc $chef_pid\] /hog.elf exited with code -137" "$LOG" && echo "  ok    and the headchef may kill any" \
    || { echo "  FAIL  the headchef could not kill pid $chef_pid"; fail=1; }
grep -qE '^\s+1\s+[RrB]' "$LOG" >/dev/null; grep -q "killed task $fbcon_id" "$LOG" && { echo "  FAIL  the kernel task was killed"; fail=1; }
# orders shows whose each job is (v0.52.3).
grep -qE '^ +[0-9]+ +R +y +- +headchef +/hog\.elf' "$LOG" && grep -qE '^ +[0-9]+ +R +y +- +cook +/hog\.elf' "$LOG" \
    && echo "  ok    orders shows each job's owner" || { echo "  FAIL  orders owner column"; grep -A3 "PID  ST  BG" "$LOG" | head -4; fail=1; }
exit $fail

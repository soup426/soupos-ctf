#!/usr/bin/env bash
# May a cook signal only their own processes? (v0.51.0)
#
# The headchef starts glutton.elf in the background and clocks out. The ordinary
# cook then tries to kill it by pid and to kill a kernel task (fbcon), and
# both must be refused; the cook's own glutton.elf they may kill. The headchef
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
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "cook glutton.elf &" "WAIT:2" >/dev/null \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
chef_pid=$(grep -oE '\[proc [0-9]+\] spawned /glutton\.elf' "$LOG" | head -1 | grep -oE '[0-9]+')
fbcon_id=$(sed -n 's/^\[fbcon\].*task \([0-9][0-9]*\).*/\1/p' "$LOG" | head -1); fbcon_id=${fbcon_id:-1}
python3 scripts/qemu_keys.py "$MON" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "kill $chef_pid" "WAIT:2" "kill $fbcon_id" "WAIT:1" \
    "cook glutton.elf &" "WAIT:2" "orders" "WAIT:1" "kill %" "WAIT:2" \
    "clockout" "WAIT:1" "headchef" "rosemary" "WAIT:2" "kill $chef_pid" "WAIT:2" >/dev/null \
    || { echo "  FAIL  could not drive QEMU (second half)"; exit 1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

grep -q "cook may not signal $chef_pid" "$LOG" && echo "  ok    a cook cannot kill the headchef's program (pid $chef_pid)" \
    || { echo "  FAIL  the cook was not refused on pid $chef_pid"; fail=1; }
grep -q "cook may not kill task $fbcon_id" "$LOG" && echo "  ok    a cook cannot kill a kernel task" \
    || { echo "  FAIL  the cook was not refused on kernel task $fbcon_id"; fail=1; }
cook_pid=$(grep -oE '\[proc [0-9]+\] spawned /glutton\.elf' "$LOG" | sed -n 2p | grep -oE '[0-9]+')
grep -q "\[proc $cook_pid\] /glutton.elf exited with code -137" "$LOG" && echo "  ok    a cook may kill their own (pid $cook_pid)" \
    || { echo "  FAIL  the cook's own program was not killed"; fail=1; }
grep -q "\[proc $chef_pid\] /glutton.elf exited with code -137" "$LOG" && echo "  ok    and the headchef may kill any" \
    || { echo "  FAIL  the headchef could not kill pid $chef_pid"; fail=1; }
grep -qE '^\s+1\s+[RrB]' "$LOG" >/dev/null; grep -q "killed task $fbcon_id" "$LOG" && { echo "  FAIL  the kernel task was killed"; fail=1; }
# orders shows whose each job is (v0.52.3).
grep -qE '^ +[0-9]+ +R +y +- +headchef +/glutton\.elf' "$LOG" && grep -qE '^ +[0-9]+ +R +y +- +cook +/glutton\.elf' "$LOG" \
    && echo "  ok    orders shows each job's owner" || { echo "  FAIL  orders owner column"; grep -A3 "PID  ST  BG" "$LOG" | head -4; fail=1; }

# ── one cook cannot fill the process table (v0.55.3) ──
# Proven on v0.55.2: the ordinary cook started eight glutton.elf and the
# headchef's `cook greet.elf` got "Too many orders". A second boot.
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
cp disk.img "$TESTDISK"; : > "$LOG"; rm -f "$MON"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
hogs() { for i in $(seq 1 "$1"); do printf '%s\n' "cook glutton.elf &" "WAIT:1"; done; }
mapfile -t COOK5 < <(hogs 5); mapfile -t SAUC3 < <(hogs 3)
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" \
    "clockout" "WAIT:1" "cook" "soup" "WAIT:2" "${COOK5[@]}" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "${SAUC3[@]}" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "cook greet.elf" "WAIT:2" "cook greet.elf &" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$LOG")
[ "$(echo "$L" | grep -c "You already have 4 orders cooking")" = 1 ] && echo "$L" | grep "^\[proc\] cook refused a process: their limit" >/dev/null \
    && echo "  ok    a cook's fifth process is refused: four each" || { echo "  FAIL  the per-cook limit"; fail=1; }
[ "$(echo "$L" | grep -c "The last orders are kept for the headchef")" = 1 ] && echo "$L" | grep "^\[proc\] saucier refused a process: the headchef's reserve" >/dev/null \
    && echo "  ok    a second cook stops at six between them, two kept for the headchef" || { echo "  FAIL  the headchef's reserve"; fail=1; }
[ "$(echo "$L" | grep -c "hello from ring 3")" = 2 ] \
    && echo "  ok    with the cooks at their limits, the headchef still cooks (twice)" || { echo "  FAIL  the headchef could not cook"; fail=1; }
exit $fail

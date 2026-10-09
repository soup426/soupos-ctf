#!/usr/bin/env bash
# status-test.sh - $? in the shell (v0.57.1).
#
# $? is replaced by the last status when its command runs. Statuses follow
# sh: a program's exit code, 127 for an unknown command, 128+n when the
# program was stopped (137 for a kill, 128+exception for a fault - sh would
# say 130 for Ctrl-C, soupOS says 137 for every kill), and 1 for a builtin
# that refused.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-status.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook call.elf hi > /T0.TXT ; cook call.elf \$? > /S1.TXT" "WAIT:2" \
    "cook greet.elf ; cook call.elf \$? > /S2.TXT" "WAIT:2" \
    "cook drop.elf ; cook call.elf \$? > /S3.TXT" "WAIT:3" \
    "nosuchthing ; cook call.elf \$? > /S4.TXT" "WAIT:2" \
    "cook greet.elf || cook call.elf \$? \$? > /S5.TXT" "WAIT:2" \
    "cook glutton.elf" "WAIT:2" "KEY:ctrl-c" "WAIT:2" "cook call.elf \$? > /S6.TXT" "WAIT:2" \
    "clockout" "WAIT:1" "cook" "soup" "WAIT:2" "reheat ; cook call.elf \$? > /home/cook/S7.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null | tr -d '\r\n'; }
crash=$(tr -d '\r' < "$WORK/serial.log" | grep -oE "/drop.elf exited with code -[0-9]+" | head -1 | grep -oE "[0-9]+$")
expect() {   # expect <file> <want> <what>
    local got; got=$(val "$1")
    if [ "$got" = "$2" ]; then echo "  ok    $3: \$? was $got"; else echo "  FAIL  $3: \$? was '$got', wanted '$2'"; fail=1; fi
}
expect /S1.TXT 0             "after a program that exits 0"
expect /S2.TXT 42            "after greet.elf, which exits 42"
expect /S3.TXT "${crash:-?}" "after drop.elf's fault (128 + exception ${crash:+$((crash - 128))})"
expect /S4.TXT 127           "after an unknown command"
expect /S5.TXT "42 42"       "twice in one command, after || (the status when it runs)"
expect /S6.TXT 137           "after Ctrl-C stops a program"
expect /home/cook/S7.TXT 1   "after a builtin the cook may not run"
exit $fail

#!/usr/bin/env bash
# clockout-test.sh - the next cook on the console starts clean (v0.60.12).
#
# The headchef leaves a command in the history, a shell variable and a line
# killed with Ctrl+U on the clipboard, then clocks out. The cook who clocks
# in next must find none of them: not in `leftovers`, not with the up
# arrow, not as $SAUCE, not with Ctrl+V.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-clockout.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
P="UNTIL:@soupOS:"
keys=("headchef" "rosemary" "WAIT:1"
  "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2"
  "SAUCE=bearnaise" "$P"
  "slurp tarragon" "$P"
  "TYPE:slurp crumb" "KEY:ctrl-u" "KEY:ctrl-c"
  "clockout" "WAIT:1" "intern" "mint" "UNTIL:Welcome to the kitchen, intern"
  "WAIT:1" "KEY:up" "KEY:up" "TYPE:slurp up-" "KEY:end" "" "$P"
  "slurp v-\$SAUCE" "$P"
  "TYPE:slurp p-" "KEY:ctrl-v" "" "$P"
  "leftovers" "$P")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
# The headchef's part, to show the test left something behind to find.
grep -q '^tarragon$' "$WORK/out.log" && echo "  ok    the headchef ran slurp tarragon" || { echo "  FAIL  the headchef's command never ran"; fail=1; }
# Everything after the intern clocked in.
sed -n '/Welcome to the kitchen, intern/,$p' "$WORK/out.log" > "$WORK/intern.log"
expect() {   # expect <line the intern should see> <what it shows>
    if grep -Fxq -- "$1" "$WORK/intern.log"; then echo "  ok    $2"
    else echo "  FAIL  $2: no line '$1'"; fail=1; fi
}
expect "up-"  "the up arrow brings back nothing from the last cook"
expect "v-"   "\$SAUCE is unset for the next cook"
expect "p-"   "Ctrl+V pastes nothing the last cook cut"
for w in tarragon bearnaise crumb; do
    if grep -q "$w" "$WORK/intern.log"; then echo "  FAIL  the next cook saw '$w':"; grep -n "$w" "$WORK/intern.log" | sed 's/^/        /'; fail=1
    else echo "  ok    the next cook never sees '$w'"; fi
done
exit $fail

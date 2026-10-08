#!/usr/bin/env bash
# search-test.sh - Ctrl+R searches the leftovers (v0.60.18).
#
# Lines are typed, then found again with Ctrl+R in each way it can be used;
# what ran is read back from `leftovers` at the end, and what the search
# line showed from the serial log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-search.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
P="UNTIL:@soupOS:"
R="KEY:ctrl-r"
keys=("headchef" "rosemary" "WAIT:1"
  "slurp apple pie" "$P" "slurp banana bread" "$P" "slurp apricot tart" "$P" "pwd" "$P"
  "$R" "TYPE:ap" "" "$P"                          # 1: the newest with "ap": apricot tart again
  "$R" "TYPE:ap" "$R" "" "$P"                     # 2: one older, past the same line: apple pie
  "TYPE:slurp kept-" "$R" "TYPE:banana" "KEY:ctrl-c" "line" "$P"   # 3: Ctrl+C gives the line back
  "$R" "TYPE:banana" "KEY:end" "TYPE: loaf" "" "$P"                # 4: End keeps the match to edit
  "$R" "TYPE:zzz" "WAIT:0.5" "KEY:ctrl-g" "slurp after-nothing" "$P" # 5: no match, then given up
  "$R" "TYPE:ap" "KEY:backspace" "" "$P"   # 6: "ap" is apple pie; Backspace to "a" is the newest again
  "leftovers" "$P")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
# The last listing: what ran, oldest first.
awk '/Leftovers \(history\):/{n=0; delete h} /^ +[0-9]+  /{sub(/^ +[0-9]+  /,""); h[++n]=$0} END{for(i=1;i<=n;i++) print h[i]}' "$WORK/out.log" > "$WORK/ran.txt"
want=("slurp apple pie" "slurp banana bread" "slurp apricot tart" "pwd"
      "slurp apricot tart" "slurp apple pie" "slurp kept-line" "slurp banana bread loaf"
      "slurp after-nothing" "slurp after-nothing" "leftovers")
printf '%s\n' "${want[@]}" > "$WORK/want.txt"
if cmp -s "$WORK/ran.txt" "$WORK/want.txt"; then
    echo "  ok    Ctrl+R ran what it found: the newest, an older one, the line given back, a match edited, Backspace"
else echo "  FAIL  what ran differs:"; diff "$WORK/want.txt" "$WORK/ran.txt" | sed 's/^/        /'; fail=1; fi
grep -q "(no leftovers)\`zzz': " "$WORK/out.log" && echo "  ok    a query that matches nothing says so" \
    || { echo "  FAIL  no '(no leftovers)\`zzz'\''' line"; fail=1; }
grep -q "(leftovers)\`ap': slurp apricot tart" "$WORK/out.log" && echo "  ok    the search line shows the query and its match" \
    || { echo "  FAIL  no '(leftovers)\`ap'\'': slurp apricot tart' line"; fail=1; }
exit $fail

#!/usr/bin/env bash
# jobspec-test.sh - job numbers (v0.60.92): two background jobs are %1 and
# %2 in orders; kill %1 and kill %% take them; rest %1 has the killed job's
# status; %3 is no such job; numbers start again at 1 once none is alive;
# a job set aside with Ctrl-Z is numbered and steep %1 and kill %1 reach it.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-jobspec.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'cook glutton.elf &' "UNTIL:@soupOS:" 'cook glutton.elf &' "UNTIL:@soupOS:" "WAIT:1"
  'orders | while take -r l ; do slurp "O:$l" ; done' "UNTIL:@soupOS:"
  'kill %1' "UNTIL:@soupOS:" 'rest %1 ; slurp J1:$?' "UNTIL:@soupOS:"
  'kill %% ; slurp J2:$?' "UNTIL:@soupOS:" "WAIT:1"
  'kill %3 ; slurp J3:$?' "UNTIL:@soupOS:"
  'cook glutton.elf &' "UNTIL:@soupOS:" "WAIT:1"
  'orders | while take -r l ; do slurp "P:$l" ; done' "UNTIL:@soupOS:"
  'kill %+' "UNTIL:@soupOS:" "WAIT:1"
  'cook glutton.elf' "WAIT:2" "KEY:ctrl-z" "UNTIL:@soupOS:"
  'steep %1' "UNTIL:@soupOS:" "WAIT:1"
  'orders | while take -r l ; do slurp "Q:$l" ; done' "UNTIL:@soupOS:"
  'kill %1' "UNTIL:@soupOS:" "WAIT:1"
  'orders | while take -r l ; do slurp "R:$l" ; done' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
check() { if echo "$L" | grep -E "$2" >/dev/null; then echo "  ok    $1"; else echo "  FAIL  $1"; fail=1; fi; }
nocheck() { if echo "$L" | grep -E "$2" >/dev/null; then echo "  FAIL  $1"; fail=1; else echo "  ok    $1"; fi; }
check "orders numbers the two jobs %1 and %2" '^O:.*glutton\.elf  %1$'
check "... and %2" '^O:.*glutton\.elf  %2$'
check "rest %1 gives the killed job's status (137)" '^J1:137$'
check "kill %% takes the newest" '^J2:0$'
check "%3 is no such job" '%3: no such job'
check "numbers start again at 1 once none is alive" '^P:.*glutton\.elf  %1$'
nocheck "... with no %2 left over" '^P:.*%2$'
check "Ctrl-Z sets one aside as %1" 'set aside .*kill %1\)'
check "steep %1 resumes it in the background" '^Q: *[0-9]+ +R +y .*glutton\.elf  %1$'
check "kill %1 takes it: it has ended, killed" '^R: *[0-9]+ +Z +y +- *137 +headchef +/glutton\.elf$'
nocheck "... and nothing of it still runs" '^R: *[0-9]+ +R .*glutton'
exit $fail

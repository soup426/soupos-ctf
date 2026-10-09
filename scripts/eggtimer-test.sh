#!/usr/bin/env bash
# eggtimer-test.sh - eggtimer (sh's time), a keyword (v0.60.87): CMD's output
# and status untouched, expanded once, a whole loop or a pipeline timed,
# skipped with what it times by && and ||, and the time itself sane
# (sleeptest sleeps 500 ms).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-eggtimer.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'eggtimer slurp E1:hi ; slurp E2:$?' "UNTIL:@soupOS:"
  'eggtimer [ 1 = 2 ] ; slurp E3:$?' "UNTIL:@soupOS:"
  "v='\$x' ; x=bad ; eggtimer slurp E4:\$v" "UNTIL:@soupOS:"
  'eggtimer for i in 1 2 ; do slurp E5:$i ; done ; slurp E6:after' "UNTIL:@soupOS:"
  '[ 1 = 2 ] && eggtimer slurp E7:no ; slurp E8:$?' "UNTIL:@soupOS:"
  'eggtimer cook call.elf E9:piped | cook spoon.elf' "UNTIL:@soupOS:"
  'eggtimer sleeptest' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^E[0-9]+:' > "$WORK/got"
printf '%s\n' E1:hi E2:0 E3:1 'E4:$x' E5:1 E5:2 E6:after E8:1 E9:piped > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    what it times prints and ends as it would ($(wc -l < "$WORK/want") lines: output, status 0 and 1, \$v expanded once, a for loop, skipped by &&, a pipeline)"
else echo "  FAIL  eggtimer changed what it timed:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
reals=$(tr -d '\r' < "$WORK/serial.log" | grep -cE $'^real\t[0-9]+m[0-9]+\\.[0-9][0-9]s$')
[ "$reals" = 6 ] && echo "  ok    six timings, one for each that ran (not the one && skipped)" || { echo "  FAIL  $reals timings, not 6"; fail=1; }
last=$(tr -d '\r' < "$WORK/serial.log" | grep -E $'^real\t' | tail -1 | sed -E 's/^real\t([0-9]+)m([0-9]+)\.([0-9]+)s$/\1 \2 \3/')
read -r m sec cs <<< "$last"
total=$(( m * 6000 + sec * 100 + 10#$cs ))
if [ "$total" -ge 48 ] && [ "$total" -le 150 ]; then echo "  ok    sleeptest's 500 ms was timed at $((total / 100)).$(printf %02d $((total % 100))) s"
else echo "  FAIL  sleeptest timed at $total hundredths"; fail=1; fi
exit $fail

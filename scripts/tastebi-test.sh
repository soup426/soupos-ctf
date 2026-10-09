#!/usr/bin/env bash
# tastebi-test.sh - taste and [ ... ] as builtins, against bash's test and [ (v0.60.79).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tastebi.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  '[ -f /tb/f.txt ] ; slurp T1:$? ; [ -d /tb ] ; slurp T2:$? ; [ -e /nope ] ; slurp T3:$?'
  '[ -z "" ] ; slurp T4:$? ; [ -n "" ] ; slurp T5:$? ; [ abc ] ; slurp T6:$? ; [ "" ] ; slurp T7:$?'
  'x=5 ; [ $x -gt 3 ] && slurp T8:yes ; [ "$x" = 5 ] ; slurp T9:$? ; [ ! $x -lt 3 ] ; slurp T10:$?'
  'taste a != b ; slurp T11:$? ; taste 10 -le 9 ; slurp T12:$?'
  '[ 1 -eq 1 ; slurp T13:$?'
  '[ x -eq 1 ] ; slurp T14:$?'
  'if [ -d /tb ] ; then slurp T15:dir ; fi'
  'n=0 ; while [ $n -lt 3 ] ; do n=$((n+1)) ; done ; slurp T16:$n'
  '[ -z $unset ] ; slurp T17:$? ; [ 99999999999 -gt 1 ] ; slurp T18:$?'
  '[ -f /tb ] ; slurp T19:$? ; [ -d /tb/f.txt ] ; slurp T20:$? ; [ -e "" ] ; slurp T21:$?'
  '[ ] ; slurp T22:$? ; [ a b c d ] ; slurp T23:$?'
)
keys=("headchef" "rosemary" "WAIT:1" "mkbowl /tb ; stir /tb/f.txt hi" "UNTIL:@soupOS:")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^T[0-9]+:' > "$WORK/got"
mkdir -p "$WORK/b/tb"; echo hi > "$WORK/b/tb/f.txt"
script=""; for l in "${LINES[@]}"; do b=${l//taste/test}; b=${b//slurp/echo}; b=${b//\/tb/$WORK\/b\/tb}; b=${b//\/nope/$WORK\/b\/nope}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^T[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    taste and [ ] give bash's test's statuses ($(wc -l < "$WORK/want") lines: -f -d -e, -z -n, one word, = !=, integers to 64 bits, !, a missing ], a bad integer, in if and while, an unset word, empty and too many)"
else echo "  FAIL  taste or [ ] differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

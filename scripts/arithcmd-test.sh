#!/usr/bin/env bash
# arithcmd-test.sh - (( expr )) and assignments in arithmetic, against bash (v0.60.50).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-arithcmd.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'n=1 ; (( n = n + 1 )) ; slurp Z1:$n'
  '(( n++ )) ; slurp Z2:$n ; (( n += 5 )) ; slurp Z3:$n'
  '(( 0 )) ; slurp Z4:$? ; (( 2 > 1 )) ; slurp Z5:$?'
  'if (( n > 3 && n < 100 )) ; then slurp Z6:yes ; fi'
  'i=0 ; while (( i < 3 )) ; do slurp Z7:$i ; (( i++ )) ; done'
  'x=5 ; slurp Z8:$((x++)):$x:$((++x)):$((x-=2))'
  '(( !0 )) && slurp Z9:not'
  '(( 1/0 )) ; slurp Z10:$?'
  '(( n || 0 )) ; slurp Z11:$?'
  '(( x = 2 + 3 * 4 )) ; slurp Z12:$x'
  'k=10 ; (( k -= 3 , k )) ; slurp Z13:$?'
  '(( y = 7 )) ; (( y *= 2 )) ; (( y %= 5 )) ; slurp Z14:$y'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^Z[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^Z[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  (( )) differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

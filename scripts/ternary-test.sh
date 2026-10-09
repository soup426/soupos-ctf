#!/usr/bin/env bash
# ternary-test.sh - && and || that stop early, and c ? a : b, in $(( )) and
# (( )), against bash (v0.60.63).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ternary.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'x=0 ; (( 0 && (x=5) )) ; slurp C1:$x:$?'
  'y=0 ; (( 1 || y++ )) ; slurp C2:$y:$?'
  'z=0 ; (( 1 && z++ )) ; slurp C3:$z:$?'
  'slurp C4:$(( 0 && 1/0 )):$(( 1 || 1/0 )):$(( 0 && 1%0 ))'
  'slurp C5:$(( 1 ? 10 : 20 )):$(( 0 ? 10 : 20 ))'
  'a=0 ; b=0 ; (( 1 ? (a=1) : (b=1) )) ; slurp C6:$a:$b'
  'slurp C7:$(( 0 ? 1 : 0 ? 2 : 3 )):$(( 1 ? 0 ? 4 : 5 : 6 ))'
  'n=5 ; slurp C8:$(( n > 3 ? n * 2 : n ))'
  'slurp C9:$(( 2 > 1 && 3 > 2 || 0 )):$(( 0 || 0 && 1 ))'
  'v=3 ; slurp C10:$(( v == 3 ? v++ : v-- )):$v'
  'w=0 ; (( w > 0 ? 1 : 0 )) ; slurp C11:$?'
  'k=0 ; slurp C12:$(( 0 ? k+=5 : k )):$k'
  'slurp C13:$(( (1 || 0) + (2 > 1 ? 5 : 6) )):$(( (j=4, j*2) ))'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^C[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^C[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    && || and ?: do what bash's do ($(wc -l < "$WORK/want") lines: no assignment, ++ or division by zero on the side not taken, nested ?:, precedence, status)"
else echo "  FAIL  arithmetic differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

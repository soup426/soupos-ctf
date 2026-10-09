#!/usr/bin/env bash
# bits-test.sh - & | ^ ~ << >> and ** in arithmetic, with &= |= ^= <<= >>=,
# against bash (v0.60.66). soupOS's arithmetic is 32 bits, bash's 64: every
# value here fits in 32.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bits.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp B1:$(( 6 & 3 )):$(( 6 | 3 )):$(( 6 ^ 3 )):$(( ~5 )):$(( ~0 ))'
  'slurp B2:$(( 1 << 4 )):$(( 256 >> 3 )):$(( -16 >> 2 )):$(( 3 << 2 + 1 ))'
  'slurp B3:$(( 2 ** 10 )):$(( 2 ** 3 ** 2 )):$(( -2 ** 2 )):$(( 7 ** 0 )):$(( 0 ** 0 ))'
  'slurp B4:$(( 1 | 2 ^ 3 & 4 )):$(( 5 & 3 == 1 )):$(( 1 + 2 << 1 )):$(( 2 * 3 ** 2 ))'
  'x=12 ; (( x &= 10 )) ; slurp B5:$x ; (( x |= 1 )) ; slurp B6:$x ; (( x ^= 15 )) ; slurp B7:$x'
  'y=3 ; (( y <<= 4 )) ; slurp B8:$y ; (( y >>= 2 )) ; slurp B9:$y'
  '(( 6 & 1 )) ; slurp B10:$? ; (( 6 & 2 )) ; slurp B11:$?'
  'f=5 ; slurp B12:$(( f & 1 ? 111 : 222 )):$(( (f | 2) ** 2 ))'
  'slurp B13:$(( 1 << 30 )):$(( 3 ** 19 ))'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^B[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^B[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    bit operators and ** do what bash's do ($(wc -l < "$WORK/want") lines: & | ^ ~ << >>, ** right to left and under unary -, precedence, &= |= ^= <<= >>=, (( )) status, with ?:)"
else echo "  FAIL  bit arithmetic differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

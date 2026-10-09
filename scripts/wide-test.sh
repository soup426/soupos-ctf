#!/usr/bin/env bash
# wide-test.sh - 64-bit arithmetic, against bash (v0.60.71): past 2**31,
# wrapping at 2**63 as bash's does, 64-bit division and remainder (libgcc's
# __divdi3 and __moddi3), shifts, ** by squaring, variables holding them;
# and bash's number forms, 0x1f, 017 and BASE#DIGITS.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-wide.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp W1:$(( 2**31 )):$(( 2**62 )):$(( 2**63 )):$(( 2**64 ))'
  'slurp W2:$(( 1 << 40 )):$(( -1 >> 70 )):$(( 3000000000 * 3 ))'
  'slurp W3:$(( 9223372036854775807 )):$(( -9223372036854775807 - 1 )):$(( 9223372036854775807 + 1 ))'
  'slurp W4:$(( 10000000000 / 7 )):$(( 10000000000 % 7 )):$(( -10000000000 / 3 )):$(( -10000000000 % 3 ))'
  'x=4000000000 ; (( x += x )) ; slurp W5:$x:$(( x / 1000 ))'
  'slurp W6:$(( 3 ** 40 )):$(( 7 ** 22 )):$(( 2 ** 0 ))'
  '(( 5000000000 > 4000000000 )) ; slurp W7:$? ; (( 2**32 == 4294967296 )) ; slurp W8:$?'
  'big=123456789012 ; slurp W9:$(( big * 1000 )):$(( big & 0xff )):${big:3:4}'
  'slurp W10:$(( 3 ** 1000000000 > 0 ))'
  'slurp W11:$(( 0x1F )):$(( 0XfF )):$(( 017 )):$(( 0 )):$(( 2#1011 )):$(( 16#ff )):$(( 36#zz )):$(( 64#_ )):$(( 64#A@ ))'
  'slurp W12:$(( 10#0042 ))'
  'slurp W13:$(( 08 ))'
  'slurp W14:$(( 2#12 ))'
)
# W13 and W14 are errors: neither prints. bash -c stops at the first one
# altogether, so they are last.
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^W[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^W[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    64-bit arithmetic is bash's ($(wc -l < "$WORK/want") lines: past 2**31, wrapping at 2**63, / and % on 64 bits, shifts, ** by squaring and a huge one done at once, (( )) status, variables, 0x hex, 017 octal, BASE#N, digits too big for the base refused)"
else echo "  FAIL  64-bit arithmetic differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

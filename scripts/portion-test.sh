#!/usr/bin/env bash
# portion-test.sh - portion.elf (factor) against the host's factor
# (v0.60.112): 0 and 1, primes, squares, 2^31-1, 2^32-1, 2^32, 2^64-1, the
# largest 64-bit prime, two 32-bit primes multiplied (rho's hard case, and
# how long it takes), stdin, and bad input (not -5, which GNU takes for
# an option and prints nothing) (status 1, the rest still done).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-portion.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# more numbers than a program's 128 bytes of arguments: through stdin
NUMS="0 1 2 97 100 360 1024 65537 2147483647 4294967295 4294967296 600851475143 9223372036854775807 18446744073709551615 18446744073709551557 1000000007000000063"
HARD=18446743979220271189
printf '%s\n' $NUMS > "$WORK/nums.txt"; mcopy -i "$WORK/disk.img" "$WORK/nums.txt" ::/nums.txt
keys=("headchef" "rosemary" "WAIT:1"
  "cook portion.elf < /nums.txt > /f1.txt" "UNTIL:@soupOS:"
  "eggtimer cook portion.elf $HARD > /f2.txt" "UNTIL:@soupOS:"
  'slurp 12 35 77 | cook portion.elf > /f3.txt' "UNTIL:@soupOS:"
  'cook portion.elf 18446744073709551616 ; slurp Q:$?' "UNTIL:@soupOS:"
  'cook portion.elf 6 x12 1.5 10 > /f4.txt ; slurp P:$?' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
cmp_() {  # cmp_ NAME WANT-COMMAND...
    local n=$1; shift
    if ! mcopy -n -i "$WORK/disk.img" "::/$n" "$WORK/g.$n" 2>/dev/null; then echo "        $n was not written"; bad=1; return; fi
    "$@" > "$WORK/w.$n" 2>/dev/null
    cmp -s "$WORK/w.$n" "$WORK/g.$n" || { echo "        $n differs:"; diff "$WORK/w.$n" "$WORK/g.$n" | sed 's/^/          /'; bad=1; }
}
cmp_ f1.txt factor $NUMS
cmp_ f2.txt factor $HARD
cmp_ f3.txt sh -c 'echo 12 35 77 | factor'
cmp_ f4.txt factor 6 x12 1.5 10
tr -d '\r' < "$WORK/serial.log" | grep -x 'P:1' >/dev/null || { echo "        bad input was not status 1"; bad=1; }
# 2^64: GNU's factor has big numbers, portion stops at 2^64-1 and says so
tr -d '\r' < "$WORK/serial.log" | grep -x 'Q:1' >/dev/null && tr -d '\r' < "$WORK/serial.log" | grep "'18446744073709551616' is not a valid positive integer" >/dev/null \
    || { echo "        2^64 was not refused with status 1"; bad=1; }
real=$(tr -d '\r' < "$WORK/serial.log" | grep -oE 'real[[:space:]]+[0-9]+m[0-9.]+s' | head -1 | tr '\t' ' ')
[ "$bad" = 0 ] && echo "  ok    portion is the host's factor (16 numbers to 2^64-1, two 32-bit primes multiplied in ${real#real }, stdin, bad input 1, 2^64 refused)" \
    || { echo "  FAIL  portion differs from factor"; fail=1; }
exit $fail

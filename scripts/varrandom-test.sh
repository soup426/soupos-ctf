#!/usr/bin/env bash
# varrandom-test.sh - $RANDOM (v0.60.58): in range, new each time, everywhere a
# variable is read. A hundred draws from 32768 repeat by chance about one
# run in seven (the birthday problem), so the test asks for 95 distinct,
# which chance misses about once in a few billion runs.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-varrandom.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'n=0 ; while (( n < 100 )) ; do slurp R:$RANDOM:${RANDOM}:$((RANDOM % 6)) ; n=$((n+1)) ; done'
  'slurp "Q:$RANDOM"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[RQ]:' > "$WORK/got"
rows=$(grep -c '^R:' "$WORK/got")
bad=$(awk -F: '/^R:/ && !($2 ~ /^[0-9]+$/ && $2 <= 32767 && $3 ~ /^[0-9]+$/ && $3 <= 32767 && $4 ~ /^[0-5]$/)' "$WORK/got" | wc -l)
distinct=$(awk -F: '/^R:/ {print $2}' "$WORK/got" | sort -u | wc -l)
same=$(awk -F: '/^R:/ && $2 == $3' "$WORK/got" | wc -l)
faces=$(awk -F: '/^R:/ {print $4}' "$WORK/got" | sort -u | wc -l)
q=$(grep -cE '^Q:[0-9]+$' "$WORK/got")
if [ "$rows" = 100 ] && [ "$bad" = 0 ]; then echo "  ok    100 rows, every \$RANDOM and \${RANDOM} in 0..32767, every \$((RANDOM % 6)) in 0..5"
else echo "  FAIL  $rows rows, $bad out of range"; fail=1; fi
if [ "$distinct" -ge 95 ] && [ "$same" -le 2 ] && [ "$faces" = 6 ]; then echo "  ok    $distinct of 100 distinct, two in one line the same $same times, all six faces of the die"
else echo "  FAIL  $distinct distinct, $same same in a line, $faces faces"; fail=1; fi
[ "$q" = 1 ] && echo "  ok    quoted \"\$RANDOM\" too" || { echo "  FAIL  quoted \$RANDOM"; fail=1; }
exit $fail

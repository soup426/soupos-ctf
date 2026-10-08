#!/usr/bin/env bash
# seq-test.sh - tally.elf against the host's seq (v0.57.7).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-seq.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
CASES=(
  "1#tally.elf 5#seq 5"
  "2#tally.elf 3 7#seq 3 7"
  "3#tally.elf 10 -3 1#seq 10 -3 1"
  "4#tally.elf 5 1#seq 5 1"
  "5#tally.elf -3 3#seq -3 3"
  "6#tally.elf -2 2 6#seq -2 2 6"
  "7#tally.elf 0#seq 0"
  "8#tally.elf 1 1#seq 1 1"
  "9#tally.elf 7 -1 7#seq 7 -1 7"
  "10#tally.elf 12 | rack.elf -r#seq 12 | LC_ALL=C sort -r"
  "11#tally.elf -s , 5#seq -s , 5"
  "12#tally.elf -s: 1 3 10#seq -s: 1 3 10"
  "13#tally.elf -w 8 11#seq -w 8 11"
  "14#tally.elf -w -10 1#seq -w -10 1"
  "15#tally.elf -w 1 3 98#seq -w 1 3 98"
  "16#tally.elf -w 007 9#seq -w 007 9"
  "17#tally.elf -w -5 -3 -11#seq -w -5 -3 -11"
  "18#tally.elf -s ab -w 1 10#seq -s ab -w 1 10"
  "19#tally.elf -s , 5 1#seq -s , 5 1"
  "20#tally.elf -w 1 +5#seq -w 1 +5"
  "21#tally.elf -s '' 3#seq -s '' 3"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /N$n.OUT" "UNTIL:headchef@soupOS:/> "); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/N$n.OUT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    bash -c "$host" > "$WORK/want$n" 2>/dev/null
    cmp -s "$WORK/got$n" "$WORK/want$n" || { echo "        differs: $soup  got [$(tr '\n' ' ' < "$WORK/got$n")]  host [$(tr '\n' ' ' < "$WORK/want$n")]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's seq (one, two, three arguments, negatives, counting down, empty runs, a pipe, -s and -w)" \
    || { echo "  FAIL  tally.elf differs from the host's seq"; fail=1; }
exit $fail

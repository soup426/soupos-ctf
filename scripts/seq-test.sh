#!/usr/bin/env bash
# seq-test.sh - tally.elf against the host's seq (v0.57.7; decimals v0.60.148).
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
  "22#tally.elf 0 0.5 2#seq 0 0.5 2"
  "23#tally.elf 1 0.25 2#seq 1 0.25 2"
  "24#tally.elf 0.1 0.1 0.5#seq 0.1 0.1 0.5"
  "25#tally.elf 1 1.50#seq 1 1.50"
  "26#tally.elf 0 1 2.5#seq 0 1 2.5"
  "27#tally.elf 0.50 1 3#seq 0.50 1 3"
  "28#tally.elf -1 0.5 1#seq -1 0.5 1"
  "29#tally.elf 2 -0.5 0#seq 2 -0.5 0"
  "30#tally.elf 1 0.3 2#seq 1 0.3 2"
  "31#tally.elf -w 1 0.5 10#seq -w 1 0.5 10"
  "32#tally.elf -w -1 0.5 1#seq -w -1 0.5 1"
  "33#tally.elf -w 0.5 10.25#seq -w 0.5 10.25"
  "34#tally.elf -s , 0 0.25 1#seq -s , 0 0.25 1"
  "35#tally.elf .5 2#seq .5 2"
  "36#tally.elf 1. 3#seq 1. 3"
  "37#tally.elf -0.5 0.5#seq -0.5 0.5"
  "38#tally.elf -w -0.5 0.25 0.5#seq -w -0.5 0.25 0.5"
  "39#tally.elf 0 0.000001 0.000003#seq 0 0.000001 0.000003"
  "40#tally.elf -w 1.5 3#seq -w 1.5 3"
  "41#tally.elf -w 1 1.50#seq -w 1 1.50"
  "42#tally.elf -w 08 0.5 10#seq -w 08 0.5 10"
  "43#tally.elf +1.5 3#seq +1.5 3"
  "44#tally.elf -w +1.5 3#seq -w +1.5 3"
  "45#tally.elf 0 0.1 1#seq 0 0.1 1"
  "46#tally.elf -w 0.10 1#seq -w 0.10 1"
  "47#tally.elf -w .5 1 2#seq -w .5 1 2"
  "48#tally.elf -w -.5 1#seq -w -.5 1"
  "49#tally.elf -w 1. 3#seq -w 1. 3"
  "50#tally.elf -.5 1#seq -.5 1"
  "51#tally.elf 1 0.3 2.05#seq 1 0.3 2.05"
  "52#tally.elf -w 0 0.05 0.1#seq -w 0 0.05 0.1"
  "53#tally.elf -w -1.25 0.5 1#seq -w -1.25 0.5 1"
  "54#tally.elf -s : -w 9.5 0.5 10.5#seq -s : -w 9.5 0.5 10.5"
  "55#tally.elf 5 -0.25 4#seq 5 -0.25 4"
  "56#tally.elf 0.5 0.5 -1#seq 0.5 0.5 -1"
  "57#tally.elf 0 0.001 3#seq 0 0.001 3"
)
# Every case from one script on the disk (v0.60.148): typing 57 lines and
# waiting for each prompt took three minutes.
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; echo "cook $soup > /N$n.OUT"; done > "$WORK/SEQ.SH"
echo "slurp SEQDONE" >> "$WORK/SEQ.SH"
mcopy -i "$WORK/disk.img" "$WORK/SEQ.SH" ::/SEQ.SH
keys=("headchef" "rosemary" "WAIT:1" ". /SEQ.SH" "UNTIL:SEQDONE")
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
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's seq (one, two, three arguments, negatives, counting down, empty runs, a pipe, -s and -w, decimals with them)" \
    || { echo "  FAIL  tally.elf differs from the host's seq"; fail=1; }
exit $fail

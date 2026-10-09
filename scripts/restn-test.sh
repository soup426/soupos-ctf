#!/usr/bin/env bash
# restn-test.sh - rest -n (wait -n) and marinate.elf (sleep, on SYS_SLEEP)
# (v0.60.101): rest -n gives the job that ends first and its status, then
# the next, then 127 with none; it waits for the quicker of two, not the
# slower; marinate sleeps what it is told, and a word is status 2.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-restn.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'eggtimer cook marinate.elf 0.5' "UNTIL:@soupOS:"
  'cook marinate.elf x ; slurp W4:$?' "UNTIL:@soupOS:"
  'cook marinate.elf 2 & cook taste.elf 1 -eq 2 & rest -n ; slurp W1:$? ; rest -n ; slurp W2:$? ; rest -n ; slurp W3:$?' "UNTIL:@soupOS:"
  'cook marinate.elf 3 & cook marinate.elf 0.5 & eggtimer rest -n ; slurp W5:$? ; rest' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
st=$(echo "$L" | grep -E '^W[0-9]+:' | tr '\n' ' ')
[ "$st" = "W4:2 W1:1 W2:0 W3:127 W5:0 " ] && echo "  ok    the first to end and its status (1), then the next (0), then 127; a word for marinate is 2" \
    || { echo "  FAIL  statuses: $st"; fail=1; }
reals=$(echo "$L" | grep -E $'^real\t' | sed -E 's/^real\t0m([0-9]+)\.([0-9]+)s$/\1\2/' | tr '\n' ' ')
read -r m r <<< "$reals"
if [ -n "${m:-}" ] && [ "$((10#$m))" -ge 48 ] && [ "$((10#$m))" -le 120 ]; then echo "  ok    marinate.elf 0.5 took $((10#$m)) hundredths of a second"; else echo "  FAIL  marinate 0.5 took '$m'"; fail=1; fi
if [ -n "${r:-}" ] && [ "$((10#$r))" -ge 40 ] && [ "$((10#$r))" -le 200 ]; then echo "  ok    rest -n came back with the 0.5 s job, not the 3 s one ($((10#$r)) hundredths)"; else echo "  FAIL  rest -n took '$r' hundredths"; fail=1; fi
exit $fail

#!/usr/bin/env bash
# takeopt-test.sh - take -p, -n and -t (v0.60.96), typed and from a pipe:
# the prompt shown (and not for a pipe), N characters without Enter, and
# the timeout's 142 with nothing typed, 0 with something typed in time.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-takeopt.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'take -p "Name? " a ; slurp T1:$a' "WAIT:1" 'bob' "UNTIL:@soupOS:"
  'take -n 3 b ; slurp T2:$b' "WAIT:1" "KEY:x" "KEY:y" "KEY:z" "UNTIL:@soupOS:"
  'take -t 1 c ; slurp T3:$?:[$c]' "UNTIL:@soupOS:"
  'take -t 0.5 d ; slurp T4:$?' "UNTIL:@soupOS:"
  'slurp abcdef | { take -n 2 e ; slurp T5:$e ; }' "UNTIL:@soupOS:"
  'slurp q | { take -p NOPE f ; slurp T6:$f ; }' "UNTIL:@soupOS:"
  'take -t 9 g ; slurp T7:$?:$g' "WAIT:1" 'hi' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep -E '^T[0-9]+:' > "$WORK/got"
printf '%s\n' T1:bob T2:xyz 'T3:142:[]' T4:142 T5:ab T6:q T7:0:hi > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    take's options do what read's do ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  take's options:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
echo "$L" | grep -F 'Name? bob' >/dev/null && echo "  ok    -p shows its prompt before what is typed" || { echo "  FAIL  no 'Name? ' prompt"; fail=1; }
echo "$L" | grep -F 'NOPE' | grep -v 'take -p NOPE' >/dev/null && { echo "  FAIL  -p prompted a pipe"; fail=1; } || echo "  ok    and not when the input is a pipe"
exit $fail

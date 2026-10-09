#!/usr/bin/env bash
# tabvar-test.sh - Tab finishes $NAME and ${NAME (v0.60.102), typed: one
# fit finished (with } after ${), two finished as far as they agree and
# listed on the second Tab, a name's case kept, commands still finished.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tabvar.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'favorite=soup ; alpha=1 ; alps=2 ; Mixed=x' "UNTIL:@soupOS:"
  'TYPE:slurp V1:$favo' "KEY:tab" "KEY:ret" "WAIT:1"
  'TYPE:slurp V2:${favo' "KEY:tab" "KEY:ret" "WAIT:1"
  'TYPE:slurp V3:$al' "KEY:tab" "WAIT:0.5" "KEY:tab" "WAIT:0.5" 'TYPE:s' "KEY:ret" "WAIT:1"
  'TYPE:slurp V4:[$mi' "KEY:tab" 'TYPE:]' "KEY:ret" "WAIT:1"
  'TYPE:slu' "KEY:tab" 'TYPE:V5:ok' "KEY:ret" "WAIT:1"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep -E '^V[0-9]:' > "$WORK/got"
printf '%s\n' V1:soup V2:soup V3:2 'V4:[]' V5:ok > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    Tab finished the names ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  Tab with variables:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
echo "$L" | grep -E '^alpha alps ?$' >/dev/null && echo "  ok    the second Tab listed alpha and alps" || { echo "  FAIL  no list of alpha and alps"; fail=1; }
exit $fail

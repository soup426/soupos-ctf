#!/usr/bin/env bash
# assign-test.sh - an assignment keeps an expanded value whole, against bash (v0.60.40).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-assign.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'v="a b" ; w=$v ; slurp "A1:$w"'
  'v="a  b" ; w=x$v ; slurp "A2:$w"'
  'f() { w=$1 ; slurp "A3:$w" ; } ; f "p  q"'
  'w=$(cook call.elf "m  n") ; slurp "A4:$w"'
  'v="a b" ; w=$v$v ; slurp "A5:$w"'
  'v="a   b" ; slurp A6:$v'
  'v="a b" ; for x in $v ; do slurp A7:$x ; done'
  'v="it'"'"'s a b" ; w=$v ; slurp "A8:$w"'
  'v="x y" ; w=pre-$v-post ; cook call.elf "A9:$w"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^A[0-9]:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//cook call.elf/echo}; script+="$b"$'\n'; done
bash -c "$script" | grep -E '^A[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints (\$v, x\$v, \$1, \$(...), \$v\$v, a quote inside, and plain words still split)"
else echo "  FAIL  assignments differ from bash (· is a space):"; diff <(sed 's/ /·/g' "$WORK/want") <(sed 's/ /·/g' "$WORK/got") | sed 's/^/        /'; fail=1; fi
exit $fail

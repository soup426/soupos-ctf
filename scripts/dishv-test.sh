#!/usr/bin/env bash
# dishv-test.sh - cook dish.elf -v NAME against bash's printf -v (v0.60.73).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-dishv.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'cook dish.elf -v x "%s-%d" ab 7 ; slurp "V1:[$x]:$?"'
  'cook dish.elf -v y "%05d|%-4s|" 42 z ; slurp "V2:[$y]"'
  'cook dish.elf -v z "a\nb\n" ; slurp "V3:[${#z}]"'
  'cook dish.elf -v w "%s," a b c ; slurp "V4:[$w]"'
  'v=hi ; cook dish.elf -v m "<%s>" "$v there" ; slurp "V5:[$m]"'
  'cook dish.elf -v 1bad x ; slurp V6:$?'
  'f() { stash r ; cook dish.elf -v r "%x" 255 ; slurp "V7:$r" ; } ; r=outer ; f ; slurp "V8:[$r]"'
  'cook dish.elf -v e "" ; slurp "V9:[$e]"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^V[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook dish.elf/printf}; b=${b//stash/local}; b=${b//slurp/echo}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^V[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    dish -v sets what bash's printf -v sets ($(wc -l < "$WORK/want") lines: %s %d widths, the format again, newlines kept, quoted \$v, a bad name refused, a stashed name, empty)"
else echo "  FAIL  dish -v differs from bash's printf -v:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

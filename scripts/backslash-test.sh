#!/usr/bin/env bash
# backslash-test.sh - \ in the shell against bash (v0.60.53).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-backslash.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp E1:a\ \ b'
  'slurp E2:\$HOME'
  'slurp "E3:q\"q"'
  'slurp "E4:back\\slash"'
  "slurp 'E5:single\\n'"
  'slurp E6:a\;b'
  'cook call.elf E7:x\|y'
  'v=val ; slurp "E8:\$v=$v"'
  'cook dish.elf "E9:%s\\n" x'
  'slurp "E10:keep \n"'
  "slurp E11:it\\'s"
  'cook call.elf E12:a\"b'
  'w=x\ \ y ; slurp "E13:$w"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^E[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//cook call.elf/echo}; b=${b//cook dish.elf/printf}; script+="$b"$'\n'; done
env -i HOME=/home/x PATH="$PATH" bash -c "$script" | grep -E '^E[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints (escaped blanks, \$, ; and |, \\\" \\\\ \\\$ inside \"...\", \\n kept there, '...' untouched, \\' outside, an assignment)"
else echo "  FAIL  backslash differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

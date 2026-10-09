#!/usr/bin/env bash
# rest-test.sh - $! and rest (wait) against bash (v0.60.48).
#
# greet.elf exits 42 (bash: sh -c 'exit 42'), taste.elf is test. Each line
# is typed on its own, as a script's lines run in bash; the W: lines must
# match.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-rest.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp W1:[$!]'
  'cook greet.elf &'
  'rest $! ; slurp W2:$?'
  'cook taste.elf 1 = 2 &'
  'rest $! ; slurp W3:$?'
  'cook greet.elf &'
  'cook taste.elf 1 = 1 &'
  'rest ; slurp W4:$?'
  'rest 9999 ; slurp W5:$?'
  'rest ; slurp W6:$?'
  'cook taste.elf 1 = 1 &'
  'x=$! ; rest $x ; slurp W7:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^W[0-9]:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook greet.elf/sh -c \'exit 42\'}; b=${b//cook taste.elf/test}; b=${b//rest/wait}; b=${b//slurp/echo}
    script+="$b"$'\n'
done
bash -c "$script" 2>/dev/null | grep -E '^W[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  rest/\$! differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

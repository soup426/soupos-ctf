#!/usr/bin/env bash
# param-test.sh - ${NAME:-word}, ${NAME:=word}, ${#NAME} against bash (v0.60.42).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-param.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp B1:${U:-dflt}'
  'E= ; slurp B2:${E:-empty}'
  'S=set ; slurp B3:${S:-no}'
  'slurp B4:${N:=new} ; slurp B5:$N'
  'L=hello ; slurp B6:${#L} ; slurp B7:${#NOPE}'
  'slurp B8:${U2:-$S-x}'
  'slurp "B9:${U3:-a  b}"'
  'f() { slurp B10:${1:-none}:${2:-two} ; } ; f one'
  'slurp B11:${S}x'
  'M=${M:-first} ; M=${M:-second} ; slurp B12:$M'
  'slurp B13:${U4:-"q r"}'
  'cook call.elf B14:${#S}'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^B[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//cook call.elf/echo}; script+="$b"$'\n'; done
env -i PATH="$PATH" bash -c "$script" | grep -E '^B[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: unset, empty, set, :=, lengths, a word with \$S and quotes, \$1, a default kept)"
else echo "  FAIL  \${...} differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

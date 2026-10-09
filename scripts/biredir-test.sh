#!/usr/bin/env bash
# biredir-test.sh - > F, >> F, < F on builtins and functions, against bash
# (v0.60.94): files byte for byte and what is printed.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-biredir.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp R1 > /r1.txt'
  'slurp R2 first > /r2.txt ; slurp R2 second >> /r2.txt'
  '> /r3.txt slurp R3 at the front'
  'f() { slurp R4:in-f $1 ; } ; f arg > /r4.txt'
  'take a b < /r2.txt ; slurp "P5:[$a][$b]"'
  'take x < /r1.txt > /r6.txt ; slurp P6:$x'
  'slurp P7:printed 2> /r7.txt'
  'slurp R8 2>&1 > /r8.txt'
  'slurp "R9 > not a redirect" > /r9.txt'
  'for i in 1 2 ; do slurp R10:$i > /r10.txt ; done'
  'v=/r11.txt ; slurp R11 > $v'
  '(( 3 > 2 )) && slurp R12:arith > /r12.txt'
  'slurp R13:$(( 1 + 2 )) > /r13.txt'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/b"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//take/read}; b=${b//\/r/$WORK\/b\/r}; script+="$b"$'\n'; done
( cd "$WORK/b" && bash -c "$script" ) 2>/dev/null | grep -E '^P[0-9]+:' > "$WORK/want"
tr -d '\r' < "$WORK/serial.log" | grep -E '^P[0-9]+:' > "$WORK/got"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { echo "        printed:"; diff "$WORK/want" "$WORK/got" | sed 's/^/          /'; bad=1; }
for k in 1 2 3 4 6 7 8 9 10 11 12 13; do
    mcopy -n -i "$WORK/disk.img" "::/r$k.txt" "$WORK/got$k" 2>/dev/null || { echo "        r$k.txt was not written"; bad=1; continue; }
    cmp -s "$WORK/got$k" "$WORK/b/r$k.txt" || { echo "        r$k.txt: got '$(tr '\n' '|' < "$WORK/got$k")', bash '$(tr '\n' '|' < "$WORK/b/r$k.txt")'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    builtins and functions take > >> < as bash's do (12 files and 3 printed lines: >, >>, at the front, a function, < into take, < and > together, 2> F empty, 2>&1, a quoted >, in a loop, \$v, after (( > )), after \$(( )) on the same line)" \
    || { echo "  FAIL  redirections on builtins differ from bash"; fail=1; }
exit $fail

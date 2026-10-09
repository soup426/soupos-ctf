#!/usr/bin/env bash
# runner-test.sh - X | runner CMD (xargs) against GNU xargs (v0.60.117):
# words batched, -n, -I {}, quotes in the input, no input (and -r), a
# program as CMD, a failing CMD (123) and a missing one (127), and 100
# words through batches that fit, each once. tally.elf is seq, stalk.elf
# dirname.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-runner.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp a b c | runner slurp X1:'
  'cook tally.elf 5 | runner -n 2 slurp X2:'
  'cook tally.elf 3 | runner -I {} slurp X3:{}-{}'
  "slurp \"'a b' c\" | runner -n 1 slurp X4:"
  'slurp -n "" | runner slurp X5:hi'
  'slurp -n "" | runner -r slurp X6:no ; slurp X6:$?'
  'slurp X7:$(slurp /a/b.txt /c/d.txt | runner cook stalk.elf)'
  'slurp a b | runner spoiled ; slurp X8:$?'
  'slurp a | runner nosuchcmd ; slurp X9:$?'
  "slurp \"it's\" | runner -I @ slurp X10:@ ; slurp X10:\$?"
  "slurp \"'a b' c\" | runner -I @ slurp X11:@"
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('cook tally.elf 100 | runner slurp B:' "UNTIL:@soupOS:")
# an unmatched quote on the second line: the first line's words still run, 1
H="$WORK/h"; mkdir -p "$H"; printf "a b\nit's\nc\n" > "$H/q.txt"; mcopy -i "$WORK/disk.img" "$H/q.txt" ::/q.txt
keys+=('cook spoon.elf /q.txt | runner slurp X12: ; slurp X12:$?' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^X[0-9]+:' "$WORK/log" > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
  b=${l//runner/xargs}; b=${b//slurp -n \"\"/printf \"\"}; b=${b//slurp/echo}; b=${b//cook tally.elf/seq}
  b=${b//cook stalk.elf/dirname}; b=${b//spoiled/false}; script+="$b"$'\n'
done
script+="xargs echo X12: < $WORK/h/q.txt ; echo X12:\$?"$'\n'
bash -c "$script" 2>/dev/null | grep -E '^X[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
# 100 words: each exactly once, in order, however they were batched
grep -E '^B: ' "$WORK/log" | sed 's/^B: //' | tr ' ' '\n' > "$WORK/got.b"
seq 100 | cmp -s - "$WORK/got.b" || { echo "        100 words did not each arrive once, in order"; bad=1; }
nb=$(grep -cE '^B: ' "$WORK/log")
[ "$bad" = 0 ] && echo "  ok    runner is GNU's xargs ($(wc -l < "$WORK/want") lines: batched, -n, -I twice, quotes both ways, an unmatched one, no input, -r, a program, 123, 127; 100 words in $nb batches, each once)" \
    || { echo "  FAIL  runner differs from xargs"; fail=1; }
exit $fail

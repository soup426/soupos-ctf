#!/usr/bin/env bash
# brew-test.sh - brew WORDS (sh's eval) against bash's eval (v0.60.110):
# a built name, a pipe held in a variable, ; and && in a string, quoting
# that lasts one round, nesting, a function, and its status.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-brew.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'i=3 ; brew "v$i=soup" ; slurp E1:$v3'
  "c='slurp E2:piped | cook raise.elf' ; brew \$c"
  "brew 'slurp E3:a ; slurp E3:b'"
  "brew 'spoiled && slurp no ; slurp E4:\$?'"
  "x='\"two  spaces\"' ; brew slurp E5:\$x"
  "brew 'brew \"slurp E6:nested\"'"
  'brew spoiled ; slurp E7:$?'
  'brew ; slurp E8:$?'
  "f() { slurp \"E9:\$1\" ; } ; brew f 'one two'"
  'for k in 1 2 ; do brew "w$k=$k$k" ; done ; slurp E10:$w1$w2'
  "brew 'slurp \"E11:\$(cook peel.elf /a/b.txt)\"'"
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
script=""; for l in "${LINES[@]}"; do b=${l//brew/eval}; b=${b//slurp/echo}; b=${b//spoiled/false}; b=${b//cook raise.elf/tr a-z A-Z}; b=${b//cook peel.elf/basename}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^E[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    brew is bash's eval ($(wc -l < "$WORK/want") lines: a built name, a pipe in a variable, ; and &&, one round of quotes, nested, a function, \$( ), status)"
else echo "  FAIL  brew differs from eval:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

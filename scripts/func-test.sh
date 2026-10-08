#!/usr/bin/env bash
# func-test.sh - shell functions against bash (v0.60.22).
#
# Each line defines and calls functions; soupOS runs it at the prompt, bash
# runs it with slurp as echo and `cook taste.elf` as test, and the F: lines
# both print must be the same, in the same order.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-func.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'greet() { slurp F:hello $1 ; slurp F:bye $2 ; } ; greet ann bob'
  'greet carol'
  'count() { slurp F:n=$# all=$@ ; } ; count a b c ; count'
  'inner() { slurp F:i=$1 ; } ; outer() { slurp F:o1=$1 ; inner x ; slurp F:o2=$1 ; } ; outer y'
  'f() { cook taste.elf 1 = 2 ; } ; f ; slurp F:st=$? ; g() { slurp F:ok ; } ; g ; slurp F:st=$?'
  'h() { slurp F:first ; } ; h() { slurp F:second ; } ; h'
  'sort3() { for x in $@ ; do case $x in a*) slurp F:A=$x ;; *) slurp F:O=$x ;; esac ; done ; } ; sort3 apple kiwi avocado'
  'v=outer ; setv() { v=inner ; } ; setv ; slurp F:v=$v'
  'w=wv ; b() { slurp F:brace=${w}-$1 ; } ; b z'
  'q() { slurp F:one=$1 two=$2 ; } ; q "a b" c'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("r() { r ; } ; r ; slurp F2:survived" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
grep -E '^F:' "$WORK/out.log" > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//slurp/echo}; b=${b//cook taste.elf/test}; b=${b//cook call.elf/echo}; b=${b//raise.elf/tr a-z A-Z}
    script+="$b"$'\n'
done
bash -c "$script" 2>/dev/null | grep -iE '^F:' | sed 's/^f:/F:/' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then
    echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: args, \$# \$@, \$1 given back, status, redefining, for and case inside, shared variables, \${w}, quoted args)"
else echo "  FAIL  functions differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
grep -q 'functions nested eight deep' "$WORK/out.log" && grep -qx 'F2:survived' "$WORK/out.log" \
    && echo "  ok    a function that calls itself stops at eight deep, and the shell carries on" \
    || { echo "  FAIL  runaway recursion was not stopped"; fail=1; }
exit $fail

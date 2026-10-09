#!/usr/bin/env bash
# amp-test.sh - & between commands, against bash (v0.60.62).
#
# Only lines whose output bash prints in one order: a job is waited for
# (rest, bash's wait) before anything else prints. A builtin before & runs
# at once in soupOS (there is no second shell to run it in), so the last
# check is soupOS's own: the & is not printed and both halves run.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-amp.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'cook taste.elf 1 -eq 2 & rest $! ; slurp A1:$?'
  'cook taste.elf 1 -eq 1 & rest $! ; slurp A2:$?'
  'cook call.elf A3:bg & rest ; slurp A4:after'
  'cook call.elf A5:x&rest'
  'cook call.elf A6:y 2>&1 & rest'
  'slurp "A7:a & b"'
  'for i in 1 2 ; do cook call.elf A8:$i & rest ; done'
  'cook call.elf A9:z & ; rest'
  'f() { slurp A10:in-f ; } ; f & rest'
  'cook taste.elf 1 -eq 1 & rest $! && slurp A11:and'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('slurp B1:one & slurp B2:two' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^A[0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook taste.elf/test}; b=${b//cook call.elf/echo}; b=${b//slurp/echo}; b=${b//rest/wait}; b=${b//& ;/\&}   # \& : a bare & in the replacement is the match (bash 5.2)
    script+="$b"$'\n'
done
bash -c "$script" 2>/dev/null | grep -E '^A[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    & does what bash's does ($(wc -l < "$WORK/want") lines: \$! and rest's status, a job then a command, no spaces, 2>&1 left alone, quoted, in a loop, & ; still, a function, &&)"
else echo "  FAIL  & differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
tr -d '\r' < "$WORK/serial.log" | grep -E '^B[0-9]:' > "$WORK/b"
if [ "$(tr '\n' ' ' < "$WORK/b")" = "B1:one B2:two " ]; then echo "  ok    a builtin before & runs, without printing the &, and the command after it runs too"
else echo "  FAIL  builtin & builtin printed: $(tr '\n' ' ' < "$WORK/b")"; fail=1; fi
exit $fail

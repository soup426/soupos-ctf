#!/usr/bin/env bash
# return-test.sh - return and exit against bash (v0.60.47).
#
# Functions at the prompt, and follow scripts on the disk that bash runs
# as `bash FILE args` (slurp as echo, taste as test); the R: lines both
# print must match. exit at the prompt is soupOS's alone (bash would end).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-return.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/soup" "$WORK/host"
printf '%s\n' 'slurp R8:start $1' 'if cook taste.elf $2 = b ; then exit 4 ; fi' 'slurp R8:never' > "$WORK/soup/S1.SH"
printf '%s\n' 'slurp R10:one' 'exit' 'slurp R10:never' > "$WORK/soup/S2.SH"
printf '%s\n' 'follow /S1.SH x b' 'slurp R11:after-inner $?' > "$WORK/soup/S3.SH"
printf '%s\n' 'f() { exit 9 ; } ; f ; slurp R13:never' 'slurp R13:never-either' > "$WORK/soup/S4.SH"
for f in S1 S2 S3 S4; do
    mcopy -i "$WORK/disk.img" "$WORK/soup/$f.SH" "::/$f.SH"
    sed -e 's/cook taste.elf/test/g; s/slurp/echo/g; s#follow /\([A-Z0-9.]*\)#bash \1#g' "$WORK/soup/$f.SH" > "$WORK/host/$f.SH"
done
LINES=(
  'f() { slurp R1:in ; return 3 ; slurp R1:never ; } ; f ; slurp R2:$?'
  'g() { cook taste.elf 1 = 2 ; return ; } ; g ; slurp R3:$?'
  'h() { for i in 1 2 3 ; do if cook taste.elf $i = 2 ; then return 7 ; fi ; slurp R4:$i ; done ; slurp R4:never ; } ; h ; slurp R5:$?'
  'return 2 ; slurp R6:$?'
  'follow /S1.SH a b ; slurp R7:$?'
  'follow /S2.SH ; slurp R9:$?'
  'follow /S3.SH'
  'follow /S4.SH ; slurp R14:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("exit ; slurp R15:still-here" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
grep -E '^R([0-9]|1[0-4]):' "$WORK/out.log" > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook taste.elf/test}; b=${b//slurp/echo}; b=$(echo "$b" | sed 's#follow /\([A-Z0-9.]*\)#bash \1#g')
    script+="$b"$'\n'
done
( cd "$WORK/host" && bash -c "$script" 2>/dev/null ) | grep -E '^R([0-9]|1[0-4]):' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: return with and without N, from a loop, outside a function, exit from a script, a nested script, from a function in a script)"
else echo "  FAIL  return/exit differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
grep -qx 'R15:still-here' "$WORK/out.log" && grep -q 'clockout ends the shift' "$WORK/out.log" \
    && echo "  ok    exit at the prompt points at clockout and the shell carries on" \
    || { echo "  FAIL  exit at the prompt"; fail=1; }
exit $fail

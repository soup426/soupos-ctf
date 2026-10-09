#!/usr/bin/env bash
# array-test.sh - arrays against bash (v0.60.88): a=(...), ${a[i]} (i
# arithmetic, -1 the last), "${a[@]}" a word each, ${a[*]}, ${#a[@]},
# ${#a[i]}, a[i]=v, $a as the first, a=v setting the first, empty, a
# function's own, discard.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-array.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'a=(x y "z w") ; slurp "R1:${a[0]}:${a[2]}:${#a[@]}:${a[-1]}"'
  'for e in "${a[@]}" ; do slurp "R2:[$e]" ; done'
  'for e in ${a[@]} ; do slurp "R3:[$e]" ; done'
  'slurp "R4:${a[*]}" ; slurp R5:$a:${a}'
  'a[1]=Y ; a[3]=end ; slurp "R6:${a[@]}:${#a[@]}"'
  'i=1 ; slurp "R7:${a[i+1]}:${a[$i]}:${#a[2]}"'
  'b=() ; for e in "${b[@]}" ; do slurp no ; done ; slurp R8:${#b[@]}'
  'a=new ; slurp "R9:${a[@]}"'
  'v="p q" ; c=($v "$v") ; slurp R10:${#c[@]}:${c[2]}'
  'f() { stash a ; a=(in) ; slurp R11:${a[0]} ; } ; f ; slurp "R12:${a[0]}:${#a[@]}"'
  'discard a ; slurp "R13:[${a[0]}]:${#a[@]}"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^R[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//stash/local}; b=${b//discard/unset}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^R[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    arrays are bash's ($(wc -l < "$WORK/want") lines: (...), [i] [-1] [i+1], \"\${a[@]}\" a word each, \${a[*]}, counts and lengths, a[i]=v, \$a, a=v, empty, split words, stash, discard)"
else echo "  FAIL  arrays differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

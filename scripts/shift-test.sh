#!/usr/bin/env bash
# shift-test.sh - shift [N] against bash (v0.60.59).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-shift.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/s.sh" <<'S'
echo T1:$1:$#
shift
echo T2:$1:$#:$@
f() { shift ; echo T3:$?:$# ; }
f
echo T4:$1
shift 3
echo T5:$?:$1:$#
shift 2
echo T6:$?:$#:[$1]
S
sed 's/\becho\b/slurp/g' "$WORK/s.sh" > "$WORK/S.SH"; mcopy -i "$WORK/disk.img" "$WORK/S.SH" ::/s.sh
LINES=(
  'f() { slurp S1:$#:$1:$2 ; shift ; slurp S2:$#:$1:$@ ; shift 2 ; slurp S3:$#:$?:$1 ; } ; f a b c d'
  'g() { shift 5 ; slurp S4:$?:$#:$1 ; } ; g x y'
  'h() { while (( $# > 0 )) ; do slurp S5:$1 ; shift ; done ; slurp S6:$# ; } ; h one two three'
  'k() { shift x ; slurp S7:$?:$1 ; shift -1 ; slurp S8:$?:$1 ; shift 0 ; slurp S9:$?:$1 ; } ; k a'
  'shift ; slurp S10:$?'
  'follow /s.sh a b c'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[ST][0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//follow \/s.sh/bash $WORK\/s.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[ST][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    shift does what bash's does ($(wc -l < "$WORK/want") lines: by one and by N, \$# and \$@ follow, too many, not a number, negative, zero, at the prompt, in a script, a function's own arguments)"
else echo "  FAIL  shift differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

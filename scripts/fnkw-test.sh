#!/usr/bin/env bash
# fnkw-test.sh - function NAME { ... } and function NAME() { ... } against
# bash (v0.60.114): one line, over several typed lines, in a recipe, with
# stash, return and $1; inspect -t says function; the word function in
# quotes or as an argument is left alone.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-fnkw.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/fn.sh" <<'SH'
function twice {
  stash v=$1
  slurp "F5:$v$v"
  return 3
}
function half() {
  slurp "F6:$1"
}
twice ab ; slurp F7:$?
half cd
SH
mcopy -i "$WORK/disk.img" "$WORK/fn.sh" ::/fn.sh
LINES=(
  'function a { slurp F1:$1 ; } ; a one'
  'function b() { slurp F2:$# ; } ; b x y z'
  'x=1 ; function c { return 4 ; } ; c ; slurp F3:$?'
  'slurp "F4:function d {" function'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('follow /fn.sh' "UNTIL:@soupOS:"
       'function g {' "WAIT:1" '  slurp "F8:$1-$2"' "WAIT:1" '}' "UNTIL:@soupOS:" 'g p q' "UNTIL:@soupOS:"
       'slurp F9:$(inspect -t a)' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^F[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
sed 's/stash/local/; s/slurp/echo/' "$WORK/fn.sh" > "$WORK/fn.bash"
script+="source $WORK/fn.bash"$'\n'
script+=$'function g {\n  echo "F8:$1-$2"\n}\ng p q\necho F9:$(type -t a)\n'
bash -c "$script" 2>/dev/null | grep -E '^F[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    function NAME { } and function NAME() { } are bash's ($(wc -l < "$WORK/want") lines: one line, typed over three, a recipe, stash return \$1 \$#, inspect -t, the word left alone)"
else echo "  FAIL  the function keyword differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

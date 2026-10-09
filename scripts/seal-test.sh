#!/usr/bin/env bash
# seal-test.sh - seal (sh's readonly) against bash's readonly (v0.60.76).
#
# bash -c leaves at the first readonly error, which soupOS's prompt does
# not do, so the bash side is written out with each failing command in an
# eval (which keeps bash going) rather than made from the soupOS lines.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-seal.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'seal a=1 ; a=2 ; slurp R1:$?:$a'
  'discard a ; slurp R2:$?:$a'
  'f() { stash a=3 ; slurp R3:in:$a ; } ; f ; slurp R3:$?'
  'seal b ; slurp R4:[$b]'
  'slurp x | while take a ; do slurp R5:no ; done ; slurp R5:$?:$a'
  'for a in x ; do slurp R6:no ; done ; slurp R6:$?:$a'
  'v="x y" ; seal c=5 d=$v ; slurp "R7:$c:$d"'
  'seal a=9 ; slurp R8:$?:$a'
  'g() { stash e=1 ; seal e ; } ; g ; e=2 ; slurp R9:$?:$e'
  'seal 9x ; slurp R10:$?'
)
BASH_SIDE=$(cat <<'B'
readonly a=1 ; eval 'a=2' ; echo R1:$?:$a
eval 'unset a' ; echo R2:$?:$a
f() { eval 'local a=3' ; echo R3:in:$a ; } ; f ; echo R3:$?
readonly b ; echo R4:[$b]
echo x | while eval 'read a' ; do echo R5:no ; done ; echo R5:$?:$a
eval 'for a in x ; do echo R6:no ; done' ; echo R6:$?:$a
v="x y" ; readonly c=5 d=$v ; echo "R7:$c:$d"
eval 'readonly a=9' ; echo R8:$?:$a
g() { local e=1 ; readonly e ; } ; g ; e=2 ; echo R9:$?:$e
eval 'readonly 9x' ; echo R10:$?
B
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('seal | while take l ; do slurp "L:$l" ; done' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^R[0-9]+:' > "$WORK/got"
bash -c "$BASH_SIDE" 2>/dev/null | grep -E '^R[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    seal does what bash's readonly does ($(wc -l < "$WORK/want") lines: =, discard, stash, take, for, sealed empty, two at once, again, a function's stash sealed and gone, not a name)"
else echo "  FAIL  seal differs from bash's readonly:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
listed=$(tr -d '\r' < "$WORK/serial.log" | grep -E '^L:' | tr '\n' ' ')
if [ "$listed" = 'L:seal a=1 L:seal b= L:seal c=5 L:seal d=x y ' ]; then echo "  ok    bare seal lists what is sealed"
else echo "  FAIL  bare seal listed: $listed"; fail=1; fi
exit $fail

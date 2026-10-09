#!/usr/bin/env bash
# stash-test.sh - stash (sh's local) against bash (v0.60.56).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-stash.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'x=global ; f() { stash x=inner ; slurp S1:$x ; } ; f ; slurp S2:$x'
  'g() { stash y=in ; slurp S3:$y ; } ; g ; slurp S4:[$y]'
  'inner() { slurp S5:$z ; } ; outer() { stash z=outerval ; inner ; } ; z=top ; outer ; slurp S6:$z'
  'r() { stash n=$1 ; if cook taste.elf $n -gt 0 ; then r $((n-1)) ; fi ; slurp S7:$n ; } ; r 3'
  'w=keep ; h() { stash w ; slurp S8:[$w] ; } ; h ; slurp S9:$w'
  'stash q=1 ; slurp S10:$?:$q'
  'v="a  b" ; k() { stash m=$v ; slurp "S11:$m" ; } ; k'
  't() { stash c=1 ; stash c=2 ; slurp S12:$c ; } ; c=0 ; t ; slurp S13:$c'
  'h2() { stash hv=inside ; cook labels.elf hv ; } ; hand hv=outside ; h2 | while take x ; do slurp S14:$x ; done ; cook labels.elf hv | while take x ; do slurp S15:$x ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^S[0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//stash/local}; b=${b//cook taste.elf/test}; b=${b//cook labels.elf/printenv}; b=${b//hand/export}; b=${b//take/read}; b=${b//slurp/echo}
    script+="$b"$'\n'
done
env -i PATH="$PATH" bash -O lastpipe -c "$script" 2>/dev/null | grep -E '^S[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash's local prints ($(wc -l < "$WORK/want") lines: shadowed and back, unset and back, dynamic scope, recursion, no value, outside a function, spaces kept, twice in one call, handed)"
else echo "  FAIL  stash differs from bash's local:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# prefix-test.sh - NAME=value CMD, a variable for one command, against bash (v0.60.41).
#
# labels.elf is env/printenv. The lines run in soupOS and in bash and the P:
# lines both print must match: the program sees the value, the shell's own
# is back as it was afterwards, and A=1 B=2 alone sets both.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-prefix.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'A=one cook labels.elf A | while take x ; do slurp P1:$x ; done ; slurp P2:$A'
  'A=keep ; A=temp cook labels.elf A | while take x ; do slurp P3:$x ; done ; slurp P4:$A'
  'X1=1 X2=2 cook labels.elf | sift.elf X | rack.elf | while take x ; do slurp P5:$x ; done'
  'C=x ; C=y slurp P6:$C ; slurp P7:$C'
  'A=1 B=2 ; slurp P8:$A$B'
  'f() { slurp P9:$Q ; } ; Q=fun f ; slurp P10:$Q'
  'hand H=orig ; H=over cook labels.elf H | while take x ; do slurp P11:$x ; done ; cook labels.elf H | while take x ; do slurp P12:$x ; done'
  'D="a b" cook labels.elf D | while take x ; do slurp "P13:$x" ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^P[0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook labels.elf | sift.elf X/env | grep X}; b=${b//cook labels.elf/printenv}
    b=${b//rack.elf/sort}; b=${b//take/read}; b=${b//hand/export}; b=${b//slurp/echo}
    script+="$b"$'\n'
done
( env -i PATH="$PATH" bash -O lastpipe -c "$script" ) | grep -E '^P[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: for one program, put back after, two at once, a builtin, a function, over a handed one, a quoted value)"
else echo "  FAIL  NAME=value CMD differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

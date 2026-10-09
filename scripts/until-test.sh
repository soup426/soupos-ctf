#!/usr/bin/env bash
# until-test.sh - until loops against bash (v0.60.43).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-until.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'n=0 ; until cook taste.elf $n -ge 3 ; do slurp U1:$n ; n=$((n+1)) ; done ; slurp U2:$n'
  'until slurp U3:once ; do slurp never ; done'
  'i=0 ; until cook taste.elf $i -eq 2 ; do j=0 ; until cook taste.elf $j -eq 2 ; do slurp U4:$i$j ; j=$((j+1)) ; done ; i=$((i+1)) ; done'
  'n=0 ; until cook taste.elf $n -eq 3 ; do slurp U5:$n ; n=$((n+1)) ; done | stack.elf'
  'cook tally.elf 3 | until take x ; do slurp never ; done ; slurp U6:$?:$x'
  'until cook taste.elf 1 = 1 ; do x=1 ; done ; slurp U7:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^U[0-9]:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook taste.elf/test}; b=${b//cook tally.elf/seq}; b=${b//stack.elf/tac}; b=${b//take/read}; b=${b//slurp/echo}
    script+="$b"$'\n'
done
bash -O lastpipe -c "$script" | grep -E '^U[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: counting up, true at once, nested, piped out and in, the status)"
else echo "  FAIL  until differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

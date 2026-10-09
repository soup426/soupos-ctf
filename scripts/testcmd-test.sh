#!/usr/bin/env bash
# testcmd-test.sh - taste.elf's exit codes against the host's test (v0.58.1).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-testcmd.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
echo here > "$WORK/E.TXT"; mcopy -i "$WORK/disk.img" "$WORK/E.TXT" ::/E.TXT; mmd -i "$WORK/disk.img" ::/D
# test arguments as typed in soupOS (paths absolute); the host runs the same
# with the leading / dropped, in a directory holding E.TXT and D/.
CASES=(
  "-e /E.TXT" "-f /E.TXT" "-d /E.TXT" "-d /D" "-f /D" "-e /D" "-e /NOPE" "-f /NOPE"
  "-z \"\"" "-z x" "-n x" "abc = abc" "abc != abc" "3 -lt 5" "5 -le 4" "-2 -gt -3" "7 -eq 7"
  "! -f /E.TXT" "! -e /NOPE" "x" "3 -lt x"
)
keys=("headchef" "rosemary" "WAIT:1")
i=0; for c in "${CASES[@]}"; do i=$((i+1)); keys+=("cook taste.elf $c ; cook call.elf \$? > /T$i.OUT" "WAIT:1"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host/D"; cp "$WORK/E.TXT" "$WORK/host/"
bad=0; i=0
for c in "${CASES[@]}"; do
    i=$((i+1))
    got=$(mcopy -n -i "$WORK/disk.img" "::/T$i.OUT" - 2>/dev/null | tr -d '\n')
    want=$(cd "$WORK/host" && sh -c "test ${c//\//}" 2>/dev/null; echo $?)
    [ "$got" = "$want" ] || { echo "        test $c: got $got, host $want"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} cases exit as the host's test does (-e -f -d, -z -n, = !=, the integer tests, !, one argument, a bad integer)" \
    || { echo "  FAIL  taste.elf differs from the host's test"; fail=1; }
exit $fail

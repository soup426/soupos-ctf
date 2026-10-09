#!/usr/bin/env bash
# cmp-test.sh - pair.elf's stdout, stderr and status against the host's cmp
# in the C locale (v0.59.4).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cmp.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"; cd "$WORK/host"
printf 'abc\ndef\n' > c1.txt; printf 'abc\ndXf\n' > c2.txt; printf 'abc\n' > c3.txt
printf 'abc\ndef\n' > c4.txt; : > c5.txt; printf 'abc' > c6.txt; printf 'abcd' > c7.txt
cd - >/dev/null
for f in c1 c2 c3 c4 c5 c6 c7; do mcopy -i "$WORK/disk.img" "$WORK/host/$f.txt" "::/$f.txt"; done
PAIRS=("c1.txt c2.txt" "c2.txt c1.txt" "c1.txt c4.txt" "c3.txt c1.txt" "c1.txt c3.txt" "c5.txt c1.txt" "c1.txt nope.txt" "c6.txt c7.txt")
keys=("headchef" "rosemary" "WAIT:1")
i=0; for pr in "${PAIRS[@]}"; do i=$((i+1)); keys+=("cook pair.elf $pr > /K$i.OUT ; cook call.elf \$? > /K$i.ST" "UNTIL:headchef@soupOS:/> "); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
bad=0; i=0; : > "$WORK/host_err"
for pr in "${PAIRS[@]}"; do
    i=$((i+1))
    hout=$(cd "$WORK/host" && LC_ALL=C cmp $pr 2>>"$WORK/host_err"); hst=$?
    [ "$(val /K$i.OUT)" = "$hout" ] && [ "$(val /K$i.ST | tr -d '\n')" = "$hst" ] \
        || { echo "        cmp $pr: stdout [$(val /K$i.OUT)] status $(val /K$i.ST | tr -d '\n'); host [$hout] $hst"; bad=1; }
done
# pair.elf is cmp under a kitchen name: its messages begin "pair:" where
# the host's begin "cmp:"; the rest must be the same.
gerr=$(tr -d '\r' < "$WORK/serial.log" | grep -E "^pair: ")
herr=$(sed 's/^cmp: /pair: /' "$WORK/host_err")
[ "$gerr" = "$herr" ] || { echo "        stderr differs:"; diff <(echo "$herr") <(echo "$gerr") | sed 's/^/          /'; bad=1; }
[ "$bad" = 0 ] && echo "  ok    all ${#PAIRS[@]} pairs give the host's stdout, stderr and status (differ both ways, same, a prefix either side, empty, missing, no final newline)" \
    || { echo "  FAIL  pair.elf differs from the host's cmp"; fail=1; }
exit $fail

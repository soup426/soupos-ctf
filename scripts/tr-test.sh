#!/usr/bin/env bash
# tr-test.sh - swap.elf against the host's tr (v0.56.9).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tr.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'Soup of the Day: 2026-10-08\nbookkeeper, aardvark, zzzz...\nabc-ABC-123 a-b-c\n  spaces   and\ttabs\n' > "$WORK/I.TXT"
mcopy -i "$WORK/disk.img" "$WORK/I.TXT" ::/I.TXT
# name # soupOS (stdin from /I.TXT) # host tr arguments, quoted for bash
CASES=(
  "1#a-z A-Z#a-z A-Z"
  "2#-d 0-9#-d 0-9"
  "3#-s a-z#-s a-z"
  "4#-s abc xyz#-s abc xyz"
  "5#-d -s 0-9 a-z#-d -s 0-9 a-z"
  "6#abc x#abc x"
  "7#aa xy#aa xy"
  "8#a-c- xyz#a-c- xyz"
  "9#0-9 '*'#0-9 '*'"   # quoted: since v0.58.3 an unquoted * is a glob, as in sh
  "10#-s .#-s ."
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook swap.elf $soup < /I.TXT > /R$n.OUT" "WAIT:2"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/R$n.OUT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && LC_ALL=C bash -c "tr $host < I.TXT" ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "        differs: swap.elf $soup"; diff "$WORK/want$n" "$WORK/got$n" | head -4 | sed 's/^/          /'; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's tr (translate, -d, -s, -d -s, padding, a repeat, a literal -, ranges)" \
    || { echo "  FAIL  swap.elf differs from the host's tr"; fail=1; }
exit $fail

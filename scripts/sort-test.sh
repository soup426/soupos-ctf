#!/usr/bin/env bash
# sort-test.sh - rack.elf against the host's LC_ALL=C sort (v0.56.3).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-sort.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# Words: case, duplicates, a prefix pair, an empty line, bytes above 127,
# and no final newline.
printf 'pear\nApple\napple\nbanana\n\nban\nbanana\nZebra\n\xc3\xa9clair\n~tilde\nlast, no newline' > "$WORK/W.TXT"
# Numbers: negatives, decimals, blanks before, -0, lines with no number,
# and ties that only the last-resort byte comparison orders.
printf '10\n9\n-3\n  7\n2.5\n2.50\n-0\n0\nabc\n100\n-10.5\n007\n7\nzzz\n3.14159\n' > "$WORK/N.TXT"
# Records (v0.60.25): blank-separated with uneven blanks and a short line;
# comma-separated with repeats, ties on the key, and an empty field.
printf 'bob 30 tea\nann  25 coffee\ncat 30 juice\ndan 7\nann 25 water\neve\t12 tea\nbob 30 tea\n' > "$WORK/K.TXT"
printf 'z,3,x\na,10,y\nm,3,x\nb,,q\na,10,y\nc,2,y\nq,10,a\n' > "$WORK/C.TXT"
for f in W N K C; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
CASES=(
  "1#rack.elf /W.TXT#sort W.TXT"
  "2#rack.elf -r /W.TXT#sort -r W.TXT"
  "3#rack.elf -n /N.TXT#sort -n N.TXT"
  "4#rack.elf -n -r /N.TXT#sort -n -r N.TXT"
  "5#spoon.elf /W.TXT | rack.elf#sort W.TXT"
  "6#rack.elf -k 2 /K.TXT#sort -k 2 K.TXT"
  "7#rack.elf -k2,2 -n /K.TXT#sort -k2,2 -n K.TXT"
  "8#rack.elf -k 3,3 /K.TXT#sort -k 3,3 K.TXT"
  "9#rack.elf -u /W.TXT#sort -u W.TXT"
  "10#rack.elf -u /K.TXT#sort -u K.TXT"
  "11#rack.elf -n -u /N.TXT#sort -n -u N.TXT"
  "12#rack.elf -t , -k 2,2 -n /C.TXT#sort -t , -k 2,2 -n C.TXT"
  "13#rack.elf -t, -k3 -r /C.TXT#sort -t, -k3 -r C.TXT"
  "14#rack.elf -u -t , -k 3,3 /C.TXT#sort -u -t , -k 3,3 C.TXT"
  "15#spoon.elf /K.TXT | rack.elf -k 2,2 -nru#sort -k 2,2 -nru K.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /S$n.TXT" "UNTIL:headchef@soupOS:/> "); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/S$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && LC_ALL=C bash -c "$host" < /dev/null ) > "$WORK/want$n"
    if cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "  ok    $soup matches LC_ALL=C $host"
    else echo "  FAIL  $soup differs from LC_ALL=C $host"; diff "$WORK/want$n" "$WORK/got$n" | head -6 | sed 's/^/        /'; fail=1; fi
done
exit $fail

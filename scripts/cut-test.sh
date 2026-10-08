#!/usr/bin/env bash
# cut-test.sh - slice.elf against the host's cut (v0.56.8).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cut.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'headchef:0:rosemary:/:x:y\ncook:1:soup:/home/cook\nno colon on this line\nsaucier:2::/home/saucier:z\n' > "$WORK/P.TXT"
printf 'one\ttwo\tthree\nalone\n\t\tthird\nx\ty\n' > "$WORK/T.TXT"
printf 'a,b,c\n,,\nd,,f\nlast,line,no newline' > "$WORK/C.TXT"
for f in P T C; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
CASES=(
  "1#slice.elf -d : -f 1 P.TXT#cut -d : -f 1 P.TXT"
  "2#slice.elf -d : -f 1,3 P.TXT#cut -d : -f 1,3 P.TXT"
  "3#slice.elf -d : -f 3,1 P.TXT#cut -d : -f 3,1 P.TXT"
  "4#slice.elf -d : -f 2- P.TXT#cut -d : -f 2- P.TXT"
  "5#slice.elf -d : -f -2 P.TXT#cut -d : -f -2 P.TXT"
  "6#slice.elf -d : -f 2-3,5 P.TXT#cut -d : -f 2-3,5 P.TXT"
  "7#slice.elf -f 2 T.TXT#cut -f 2 T.TXT"
  "8#slice.elf -d , -f 2 C.TXT#cut -d , -f 2 C.TXT"
  "9#spoon.elf P.TXT | slice.elf -d : -f 1,7#cut -d : -f 1,7 P.TXT"
  "10#slice.elf -d: -f1 P.TXT#cut -d: -f1 P.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /K$n.OUT" "WAIT:2"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/K$n.OUT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && LC_ALL=C bash -c "$host" < /dev/null ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "        differs: $soup"; diff "$WORK/want$n" "$WORK/got$n" | head -4 | sed 's/^/          /'; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's cut (lists in any order, open ranges, tabs, no delimiter, empty fields, no final newline, a pipe)" \
    || { echo "  FAIL  slice.elf differs from the host's cut"; fail=1; }
exit $fail

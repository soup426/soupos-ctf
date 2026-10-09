#!/usr/bin/env bash
# knead-test.sh - knead.elf against the host's fold, byte for byte (v0.60.15).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-knead.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
export LC_ALL=C
cp disk.img "$WORK/disk.img"
# Prose with long and short words, tabs, a word longer than any width, a
# backspace and a carriage return, a long line, and no final newline.
{ echo "The stock simmers for hours while the cook skims the fat and tastes it now and then."
  printf 'a\tb\tc\td\te\tf\tg\th\ti\tj\tk\tl\n'
  echo "supercalifragilisticexpialidociousbroth is one word"
  printf 'over\b\b\bstrike and back\rcarriage return line that is long enough to fold\n'
  printf '  leading blanks then words\n\n'
  seq 1 400 | tr '\n' ' '; echo
  printf 'no final newline here, still long enough to need a fold or two' ; } > "$WORK/F1.TXT"
: > "$WORK/F2.TXT"
for f in F1 F2; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#knead.elf /F1.TXT#fold F1.TXT"
  "2#knead.elf -w 20 /F1.TXT#fold -w 20 F1.TXT"
  "3#knead.elf -s -w 20 /F1.TXT#fold -s -w 20 F1.TXT"
  "4#knead.elf -w5 /F1.TXT#fold -w5 F1.TXT"
  "5#knead.elf -w 1 /F1.TXT#fold -w 1 F1.TXT"
  "6#knead.elf -s -w 7 /F1.TXT#fold -s -w 7 F1.TXT"
  "7#knead.elf -w 33 -s /F1.TXT#fold -w 33 -s F1.TXT"
  "8#spoon.elf /F1.TXT | knead.elf -s -w 12#fold -s -w 12 F1.TXT"
  "9#knead.elf /F2.TXT#fold F2.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
keys+=("cook knead.elf /NOPE.TXT" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/H$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && bash -c "$host" < /dev/null ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "        differs: $soup  (host: $host)"; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's fold byte for byte (widths 1 to 80, -s, tabs, backspace, carriage return, a word wider than the width, no final newline, a pipe)" \
    || { echo "  FAIL  knead differs from the host's fold"; fail=1; }
if grep -q 'knead: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'knead: cannot open' for a missing file"; fail=1; fi
exit $fail

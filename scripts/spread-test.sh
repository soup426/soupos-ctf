#!/usr/bin/env bash
# spread-test.sh - spread.elf against the host's expand, byte for byte (v0.60.24).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-spread.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
export LC_ALL=C
cp disk.img "$WORK/disk.img"
# Tabs at every column, runs of them, backspace and carriage return beside
# them, a line wider than 4 KB, no final newline.
{ printf 'a\tb\tc\n\tlead\n\t\t\tthree\n'
  printf 'abcdefg\th\nabcdefgh\ti\n'
  printf 'ab\b\tc\nab\r\tc\n'
  for i in $(seq 1 600); do printf 'x\t'; done; echo
  printf 'no final newline\there' ; } > "$WORK/F1.TXT"
: > "$WORK/F2.TXT"
for f in F1 F2; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#spread.elf /F1.TXT#expand F1.TXT"
  "2#spread.elf -t 4 /F1.TXT#expand -t 4 F1.TXT"
  "3#spread.elf -t1 /F1.TXT#expand -t1 F1.TXT"
  "4#spread.elf -t 13 /F1.TXT#expand -t 13 F1.TXT"
  "5#spread.elf /F2.TXT#expand F2.TXT"
  "6#spoon.elf /F1.TXT | spread.elf -t 3#expand -t 3 F1.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
keys+=("cook spread.elf /NOPE.TXT" "WAIT:2")
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
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's expand byte for byte (stops of 1 to 13, runs of tabs, backspace, carriage return, a line past 4 KB, no final newline, a pipe)" \
    || { echo "  FAIL  spread differs from the host's expand"; fail=1; }
if grep -q 'spread: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'spread: cannot open' for a missing file"; fail=1; fi
exit $fail

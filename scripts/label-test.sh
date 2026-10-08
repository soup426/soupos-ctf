#!/usr/bin/env bash
# label-test.sh - label.elf against the host's nl -ba, byte for byte (v0.60.9).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-label.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
export LC_ALL=C
cp disk.img "$WORK/disk.img"
seq 1 25 | sed 's/^/line /' > "$WORK/F1.TXT"                      # 25 lines
printf 'one\n\n  two  \nthree, no newline' > "$WORK/F2.TXT"        # empty line, spaces, no final newline
: > "$WORK/F3.TXT"                                                 # empty
printf '\n\n\n' > "$WORK/F4.TXT"                                   # only newlines
head -c 6000 /dev/zero | tr '\0' 'a' > "$WORK/LONG.TXT"; printf 'Z\nend\n' >> "$WORK/LONG.TXT"  # one line past 4 KB
seq 1 2000 | sed 's/$/ of the big one/' > "$WORK/BIG.TXT"
for f in F1 F2 F3 F4 LONG BIG; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#label.elf /F1.TXT#nl -ba F1.TXT"
  "2#label.elf /F2.TXT#nl -ba F2.TXT"
  "3#label.elf /F3.TXT#nl -ba F3.TXT"
  "4#label.elf /F4.TXT#nl -ba F4.TXT"
  "5#label.elf /LONG.TXT#nl -ba LONG.TXT"
  "6#label.elf /BIG.TXT#nl -ba BIG.TXT"
  "7#spoon.elf /F2.TXT | label.elf#nl -ba F2.TXT"
  "8#label.elf /F1.TXT | label.elf#nl -ba F1.TXT | nl -ba"
  "9#tally.elf 5 | label.elf | label.elf#seq 5 | nl -ba | nl -ba"
  "10#tally.elf 1000000 | label.elf | sift.elf 999999#seq 1000000 | nl -ba | grep 999999"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
keys+=("cook label.elf /NOPE.TXT" "WAIT:2")
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
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's nl -ba byte for byte (empty lines numbered, a newline added at the end, empty file, a 6 KB line, pipes, seven-digit numbers)" \
    || { echo "  FAIL  label differs from the host's nl -ba"; fail=1; }
if grep -q 'label: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'label: cannot open' for a missing file"; fail=1; fi
exit $fail

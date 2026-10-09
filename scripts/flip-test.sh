#!/usr/bin/env bash
# flip-test.sh - flip.elf against the host's rev, byte for byte (v0.60.8).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-flip.XXXXXX)
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
  "1#flip.elf /F1.TXT#rev F1.TXT"
  "2#flip.elf /F2.TXT#rev F2.TXT"
  "3#flip.elf /F3.TXT#rev F3.TXT"
  "4#flip.elf /F4.TXT#rev F4.TXT"
  "5#flip.elf /LONG.TXT#rev LONG.TXT"
  "6#flip.elf /BIG.TXT#rev BIG.TXT"
  "7#spoon.elf /F2.TXT | flip.elf#rev F2.TXT"
  "8#flip.elf /BIG.TXT | flip.elf#cat BIG.TXT"
  "9#call.elf stressed | flip.elf#echo stressed | rev"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "WAIT:2"); done
keys+=("cook flip.elf /NOPE.TXT" "WAIT:2")
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
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's rev byte for byte (empty lines, no final newline, empty file, a 6 KB line, pipes, flipped twice)" \
    || { echo "  FAIL  flip differs from the host's rev"; fail=1; }
if grep -q 'flip: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'flip: cannot open' for a missing file"; fail=1; fi
exit $fail

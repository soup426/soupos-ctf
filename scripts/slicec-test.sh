#!/usr/bin/env bash
# slicec-test.sh - slice -c / -b LIST and -s (cut -c, cut -b, cut -s)
# against GNU cut (v0.60.127): N, N-M, N-, -M, lists out of order and
# overlapping, past the line's end, empty lines, a last line with no
# newline, and -s with -f and -d.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-slicec.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'abcdefghij\nshort\n\nroot:x:0:0:root\nno colons here\nabc' > "$H/t.txt"
mcopy -i "$WORK/disk.img" "$H/t.txt" ::/t.txt
CASES=("-c 1" "-c 3-5" "-c 8-" "-c -3" "-c 5,1,3" "-c 2-4,3-6" "-c 20" "-b 2,4" "-c 1-2,9-"
       "-s -d : -f 1" "-d : -s -f 2,4" "-d : -f 1" "-s -f 1")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook slice.elf %s /t.txt\n' $k "$c" >> "$H/r.sh"; done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/^cook slice.elf/cut/; s| /t.txt$| t.txt|' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash) > "$WORK/want" 2>/dev/null
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    slice -c/-b/-s are GNU cut's ($k cases, $(wc -l < "$WORK/want") lines alike)"
else echo "  FAIL  slice differs from cut:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# weighfiles-test.sh - weigh over several files, a total line and -L,
# against GNU wc (v0.60.135): every option alone and together, widths from
# the total bytes, tabs in -L, a missing file among them (status 1), one
# file, stdin, an empty file.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-weighfiles.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'one two three\nfour\n' > "$H/a.txt"
seq -f 'line number %g of many' 300 > "$H/b.txt"
printf 'a\tb\n\tx\nabcdefghi\tz\nlast line has no newline' > "$H/t.txt"
: > "$H/e.txt"
for f in a.txt b.txt t.txt e.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
CASES=("a.txt b.txt" "-l a.txt b.txt" "-w a.txt b.txt" "-c a.txt b.txt t.txt" "-L a.txt b.txt t.txt" "-lL t.txt"
       "-L t.txt" "-lwcL a.txt t.txt" "a.txt e.txt" "-l a.txt nope.txt b.txt" "e.txt" "-L e.txt a.txt")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook weigh.elf %s\nslurp "st $?"\n' $k "$c" >> "$H/r.sh"; done
printf 'slurp "== in"\ncook spoon.elf t.txt | cook weigh.elf -L\n' >> "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'cd / ; follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/cook weigh.elf/wc/; s/cook spoon.elf/cat/' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash 2>/dev/null) > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    weigh over several files is GNU wc ($k cases and stdin, $(wc -l < "$WORK/want") lines with statuses: totals, widths, -L with tabs, a missing file 1, one file, empty)"
else echo "  FAIL  weigh differs from wc:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# nlopts-test.sh - label's nl options against GNU nl (v0.60.138): -b a, t
# and n, -w, -s, -v, -i, -n ln rn rz, together, and from stdin; empty
# lines and a last line with no newline in the input. With no -b, label
# numbers every line (its default since v0.60.9), so nl gets -ba then.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-nlopts.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'first\n\nthird\n\n\nsixth line is longer\nseventh\nlast no newline' > "$H/t.txt"
mcopy -i "$WORK/disk.img" "$H/t.txt" ::/t.txt
CASES=("-b a t.txt" "-b t t.txt" "-b n t.txt" "-bt t.txt" "-w 3 t.txt" "-w2 -b a t.txt" "-s :: t.txt" "-s '' -w 1 t.txt"
       "-v 0 t.txt" "-v 10 -i 5 t.txt" "-i -2 -v 3 t.txt" "-n ln t.txt" "-n rz t.txt" "-n rz -w 3 -b a t.txt"
       "-n ln -w 4 -s ' | ' -b t t.txt" "-w 1 -v 98 -b a t.txt")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook label.elf %s\n' $k "$c" >> "$H/r.sh"; done
printf 'slurp "== in"\ncook spoon.elf t.txt | cook label.elf -b t -n rz\n' >> "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'cd / ; follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
# label numbers every line unless told (nl -ba); GNU nl's default is -bt,
# so a case with no -b is given -ba on the nl side
sed 's/^slurp /echo /; s/cook spoon.elf/cat/; /cook label.elf.*-b/s/cook label.elf/nl/; s/cook label.elf/nl -ba/' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash 2>/dev/null) > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    label's options are GNU nl's ($k cases and stdin, $(wc -l < "$WORK/want") lines: -b a t n, -w, -s, -v, -i, -n ln rn rz, together)"
else echo "  FAIL  label differs from nl:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

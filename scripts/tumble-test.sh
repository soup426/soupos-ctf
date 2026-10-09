#!/usr/bin/env bash
# tumble-test.sh - tumble.elf (shuf) by its properties, as randomness has no
# expected bytes (v0.60.118): a file's lines and stdin's come out each
# exactly once, two runs differ, -n 5 gives five distinct of them, -i 1-20
# a permutation of 1..20, nothing in gives nothing, a last line gets its
# newline, a bad range or count is status 1 (as GNU's), and over 200 draws
# each of four lines comes first a fair share of the time.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tumble.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
seq -f 'line %g' 50 > "$H/l.txt"; : > "$H/e.txt"; printf 'a\nb\nc' > "$H/n.txt"; printf 'w\nx\ny\nz\n' > "$H/f.txt"
for f in l.txt e.txt n.txt f.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
keys=("headchef" "rosemary" "WAIT:1"
  'cook tumble.elf /l.txt > /t1.txt ; cook tumble.elf /l.txt > /t2.txt' "UNTIL:@soupOS:"
  'cook spoon.elf /l.txt | cook tumble.elf > /t3.txt' "UNTIL:@soupOS:"
  'cook tumble.elf -n 5 /l.txt > /t4.txt ; cook tumble.elf -i 1-20 > /t5.txt' "UNTIL:@soupOS:"
  'cook tumble.elf /e.txt > /t6.txt ; cook tumble.elf /n.txt > /t7.txt' "UNTIL:@soupOS:"
  'cook tumble.elf -i 5-3 ; slurp S1:$? ; cook tumble.elf -n x /l.txt ; slurp S2:$?' "UNTIL:@soupOS:"
  'for i in {1..200} ; do cook tumble.elf -n 1 /f.txt ; done > /t8.txt' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for f in t1 t2 t3 t4 t5 t6 t7 t8; do mcopy -n -i "$WORK/disk.img" "::/$f.txt" "$WORK/$f" 2>/dev/null || { echo "        $f.txt was not written"; bad=1; touch "$WORK/$f"; }; done
sort "$H/l.txt" > "$WORK/l.sorted"
for f in t1 t2 t3; do sort "$WORK/$f" | cmp -s - "$WORK/l.sorted" || { echo "        $f is not the 50 lines each once"; bad=1; }; done
cmp -s "$WORK/t1" "$WORK/l.txt" && { echo "        t1 came out in the order it went in"; bad=1; }
cmp -s "$WORK/t1" "$WORK/t2" && { echo "        two runs gave the same order"; bad=1; }
[ "$(wc -l < "$WORK/t4")" = 5 ] && [ "$(sort -u "$WORK/t4" | wc -l)" = 5 ] && [ -z "$(comm -23 <(sort "$WORK/t4") "$WORK/l.sorted")" ] \
    || { echo "        -n 5 is not five distinct input lines"; bad=1; }
sort -n "$WORK/t5" | cmp -s - <(seq 20) || { echo "        -i 1-20 is not a permutation of 1..20"; bad=1; }
[ ! -s "$WORK/t6" ] || { echo "        nothing in did not give nothing"; bad=1; }
[ "$(sort "$WORK/t7" | tr '\n' ,)" = "a,b,c," ] || { echo "        a last line without a newline: $(od -c "$WORK/t7" | head -2)"; bad=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x 'S1:1' "$WORK/log" >/dev/null && grep -x 'S2:1' "$WORK/log" >/dev/null || { echo "        a bad range or count was not status 1"; bad=1; }
# 200 draws of one line from four: each expected 50; outside 20..80 is a
# 1-in-a-billion event for a fair draw, and the certain one for a broken one
counts=$(sort "$WORK/t8" | uniq -c | awk '{printf "%s=%s ", $2, $1}')
[ "$(wc -l < "$WORK/t8")" = 200 ] || { echo "        200 draws gave $(wc -l < "$WORK/t8") lines"; bad=1; }
for x in w x y z; do c=$(grep -cx "$x" "$WORK/t8"); [ "$c" -ge 20 ] && [ "$c" -le 80 ] || { echo "        $x came first $c times of 200 ($counts)"; bad=1; }; done
[ "$bad" = 0 ] && echo "  ok    tumble shuffles as shuf: permutations from a file and stdin, runs differ, -n, -i, empty, last newline, bad input 1; first of four over 200: $counts" \
    || { echo "  FAIL  tumble is not shuf"; fail=1; }
exit $fail

#!/usr/bin/env bash
# stock-test.sh - take -a (read -a) and stock (mapfile) against bash
# (v0.60.121), from <<< and < FILE; and from a pipe, where soupOS keeps
# what bash's subshell loses, checked against what was sent.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-stock.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'one two\nthree\nfour five six\n' > "$H/w.txt"; : > "$H/e.txt"; printf 'x\ny' > "$H/n.txt"
for f in w.txt e.txt n.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
LINES=(
  'take -a ar <<< "a b  c" ; slurp A1:${#ar[@]} ${ar[1]} ${ar[2]}'
  'take -a ar <<< "" ; slurp A2:${#ar[@]} $?'
  "take -r -a ar <<< 'x\\y z' ; slurp \"A3:\${ar[0]}\""
  'take -a ar < /w.txt ; slurp A4:${ar[@]}'
  'stock -t m < /w.txt ; slurp A5:${#m[@]} ${m[0]} ${m[2]}'
  'stock m < /w.txt ; slurp A6:${#m[0]} ${#m[1]}'
  'stock -t <<< "p q" ; slurp A7:${MAPFILE[0]} ${#MAPFILE[@]}'
  'stock -t e < /e.txt ; slurp A8:${#e[@]} $?'
  'stock n < /n.txt ; slurp A9:${#n[@]} ${#n[0]} ${#n[1]}'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('cook spoon.elf /w.txt | stock -t pm ; slurp P1:${#pm[@]} ${pm[1]}' "UNTIL:@soupOS:"
       'slurp g h i | take -a pa ; slurp P2:${#pa[@]} ${pa[2]}' "UNTIL:@soupOS:"
       'slurp j k | take u v ; slurp P3:$u-$v' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^A[0-9]+:' "$WORK/log" > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//take/read}; b=${b//stock/mapfile}; b=${b//slurp/echo}; b=${b// \// $H\/}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^A[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
for want in 'P1:3 three' 'P2:3 i' 'P3:j-k'; do grep -x "$want" "$WORK/log" >/dev/null || { echo "        from a pipe: no '$want'"; bad=1; }; done
[ "$bad" = 0 ] && echo "  ok    take -a and stock are bash's read -a and mapfile ($(wc -l < "$WORK/want") lines: <<<, < FILE, -r, -t, MAPFILE, empty, no last newline; and from a pipe, kept)" \
    || { echo "  FAIL  take -a or stock differ"; fail=1; }
exit $fail

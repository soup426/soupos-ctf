#!/usr/bin/env bash
# taked-test.sh - take -d DELIM (read -d) against bash (v0.60.137): a
# delimiter, words across it (a newline among them parts words as IFS's
# does), -d '' to the end (status 1), -a, -r, -n, from < FILE; and from a
# pipe, where soupOS keeps what bash's subshell loses.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-taked.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'line1\nline2.tail' > "$H/m.txt"
mcopy -i "$WORK/disk.img" "$H/m.txt" ::/m.txt
LINES=(
  'take -d : x <<< "a b:c" ; slurp "D1:[$x] $?"'
  'take -d : x y <<< "one two three:rest" ; slurp "D2:[$x][$y] $?"'
  "take -d '' x <<< \"abc\" ; slurp \"D3:[\$x] \$?\""
  'take -d , -a arr <<< "p q,r s" ; slurp "D4:${#arr[@]} ${arr[1]}"'
  'take -d . a b < /m.txt ; slurp "D5:[$a][$b] $?"'
  "take -r -d x v <<< 'a\\bxc' ; slurp \"D6:[\$v]\""
  "take -d x v <<< 'a\\bxc' ; slurp \"D7:[\$v]\""
  'take -n 3 -d : v <<< "abcdef:" ; slurp "D8:[$v] $?"'
  'take -d z v <<< "no delimiter here" ; slurp "D9:[$v] $?"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('slurp "u:v" | take -d : p ; slurp "P1:[$p]"' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^D[0-9]+:' "$WORK/log" > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//take/read}; b=${b//slurp/echo}; b=${b// \/m.txt/ $H\/m.txt}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^D[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
grep -x 'P1:\[u\]' "$WORK/log" >/dev/null || { echo "        from a pipe: $(grep '^P1' "$WORK/log")"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    take -d is bash's read -d ($(wc -l < "$WORK/want") lines: a delimiter, words across it, -d '' to the end, -a, -r, -n, < FILE, none found 1; and a pipe, kept)" \
    || { echo "  FAIL  take -d differs from read -d"; fail=1; }
exit $fail

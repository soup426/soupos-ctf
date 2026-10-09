#!/usr/bin/env bash
# taker-test.sh - take's backslashes and take -r, against bash's read and
# read -r (v0.60.67): \x is x, `\ ` keeps a space in a word, \ at the end
# of a line joins the next, tabs part words; -r keeps every backslash.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-taker.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf '%s\n' 'a\ b c' 'back\\slash \x y' $'\t lead  tab\tsep  ' 'joined \' 'next line' 'two\ \ sp  rest  of it ' 'end\' > "$WORK/t.txt"
mcopy -i "$WORK/disk.img" "$WORK/t.txt" ::/t.txt
LINES=(
  'while take x y ; do slurp "K:[$x][$y]" ; done < /t.txt'
  'while take -r x y ; do slurp "R:[$x][$y]" ; done < /t.txt'
  'while take l ; do slurp "L:[$l]" ; done < /t.txt'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('take a b ; slurp "T:[$a][$b]"' 'p\ q r' "UNTIL:@soupOS:" 'take -r a b ; slurp "T:[$a][$b]"' 'p\ q r' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[KRLT]:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//take/read}; b=${b//slurp/echo}; b=${b//\/t.txt/$WORK\/t.txt}; script+="$b"$'\n'; done
script+=$'read a b <<< \'p\\ q r\' ; echo "T:[$a][$b]"\nread -r a b <<< \'p\\ q r\' ; echo "T:[$a][$b]"\n'
bash -c "$script" 2>/dev/null | grep -E '^[KRLT]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    take and take -r read what bash's read and read -r do ($(wc -l < "$WORK/want") lines: \\ space, \\\\, \\x, tabs, a joined line, trailing blanks, a last line with no newline, typed too)"
else echo "  FAIL  take differs from bash's read:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

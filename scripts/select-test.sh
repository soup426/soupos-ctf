#!/usr/bin/env bash
# select-test.sh - select NAME in WORDS (v0.60.103): fed from a file
# against bash (a number, one out of range, a blank line showing the menu
# again, the end of input, status 1), and typed with $PS3 and break.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-select.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf '2\n9\n\n1\n' > "$WORK/in.txt"; mcopy -i "$WORK/disk.img" "$WORK/in.txt" ::/in.txt
keys=("headchef" "rosemary" "WAIT:1"
  'select x in a b c ; do slurp "S:$x:$REPLY" ; done < /in.txt ; slurp S:status:$?' "UNTIL:@soupOS:"
  'PS3="pick: " ; select y in p q ; do slurp "T:$y" ; break ; done ; slurp T:status:$?' "WAIT:1" '2' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep -E '^S:' > "$WORK/got"
( cd "$WORK" && bash -c 'select x in a b c ; do echo "S:$x:$REPLY" ; done < in.txt ; echo S:status:$?' ) 2>/dev/null | grep -E '^S:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    select fed a file does what bash's does ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  select differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
menus=$(echo "$L" | grep -c '^1) a$')
[ "$menus" = 2 ] && echo "  ok    the menu was shown at the start and again for the blank line" || { echo "  FAIL  the menu was shown $menus times, not 2"; fail=1; }
t=$(echo "$L" | grep -E '^T:' | tr '\n' ' ')
[ "$t" = "T:q T:status:0 " ] && echo "  ok    typed: the second word, then break, status 0" || { echo "  FAIL  typed: $t"; fail=1; }
echo "$L" | grep -F 'pick: ' >/dev/null && echo "  ok    \$PS3 was the prompt" || { echo "  FAIL  no 'pick: ' prompt"; fail=1; }
exit $fail

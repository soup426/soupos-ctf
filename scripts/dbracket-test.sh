#!/usr/bin/env bash
# dbracket-test.sh - [[ EXPR ]] against bash's (v0.60.113): no splitting of
# a variable holding blanks, == and != with patterns (quoted: text), < >,
# && || ! ( ) inside, short-circuit (no expansion past a false &&), taste's
# tests, in if / while / with && after it, and a malformed one (status 2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-dbracket.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'x="a b c" ; [[ $x == "a b c" ]] ; slurp D1:$?'
  'f=soup.txt ; [[ $f == *.txt ]] ; slurp D2:$? ; [[ $f == "*.txt" ]] ; slurp D3:$?'
  'p="s*" ; [[ soup == $p ]] ; slurp D4:$? ; [[ soup != s?up ]] ; slurp D5:$?'
  '[[ apple < banana ]] ; slurp D6:$? ; [[ apple > banana ]] ; slurp D7:$?'
  '[[ 1 -eq 1 && 2 -gt 3 ]] ; slurp D8:$? ; [[ 1 -eq 2 || 2 -gt 1 ]] ; slurp D9:$?'
  '[[ ! -z "x" ]] ; slurp D10:$? ; [[ -n "" ]] ; slurp D11:$?'
  '[[ ( 1 -eq 2 || 3 -eq 3 ) && ! 4 -eq 5 ]] ; slurp D12:$?'
  'n=0 ; [[ 1 -eq 2 && $((n=5)) -eq 5 ]] ; slurp D13:$n'
  'if [[ -f /nums.txt || -d / ]] ; then slurp D14:yes ; else slurp D14:no ; fi'
  'i=0 ; while [[ $i -lt 3 ]] ; do i=$((i+1)) ; done ; slurp D15:$i'
  '[[ abc == a* ]] && slurp D16:and ; [[ abc == b* ]] || slurp D17:or'
  'e="" ; [[ $e ]] ; slurp D18:$? ; [[ "$e" == "" ]] ; slurp D19:$?'
  '[[ 3 -lt 4 ]] ; slurp D20:$? ; ! [[ 3 -lt 4 ]] ; slurp D21:$?'
  'y="1 -eq 1" ; [[ -n $y ]] ; slurp D22:$?'
  '[[ abc == "a"* ]] ; slurp D23:$? ; [[ a*c == "a*"c ]] ; slurp D26:$? ; [[ abc == "a*"c ]] ; slurp D27:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('[[ 1 -eq ]] ; slurp D24:$?' "UNTIL:@soupOS:" '[[ a == b' "UNTIL:@soupOS:" 'slurp D25:$?' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^D[0-9]+:' | grep -vE '^D2[45]:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; script+="$b"$'\n'; done
(cd "$WORK" && bash -c "$script" 2>/dev/null) | grep -E '^D[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
# malformed: bash says so and gives 2 ([[ a == b with no ]] is a syntax
# error there, 2 at the next line); soupOS gives 2 for both
tr -d '\r' < "$WORK/serial.log" | grep -x 'D24:2' >/dev/null || { echo "        [[ 1 -eq ]] was not status 2"; bad=1; }
tr -d '\r' < "$WORK/serial.log" | grep -x 'D25:2' >/dev/null || { echo "        [[ with no ]] was not status 2"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    [[ ]] is bash's ($(wc -l < "$WORK/want") lines: no splitting, patterns and quoted text, < >, && || ! ( ), short-circuit, if/while/&&; malformed 2)" \
    || { echo "  FAIL  [[ ]] differs from bash"; fail=1; }
exit $fail

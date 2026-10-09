#!/usr/bin/env bash
# rematch-test.sh - [[ STRING =~ REGEX ]] and BASH_REMATCH against bash
# (v0.60.120): groups, a regex in a variable, no match (BASH_REMATCH
# emptied), quoted text as literal, a bad regex (2), a group that took no
# part, =~ beside &&, and \ before a blank.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-rematch.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  '[[ "soup123" =~ ^[a-z]+([0-9]+)$ ]] ; slurp R1:$? ${BASH_REMATCH[0]} ${BASH_REMATCH[1]}'
  're="([a-z]+)@([a-z]+)\.com" ; [[ "mail chef@soup.com now" =~ $re ]] ; slurp R2:$? ${BASH_REMATCH[@]}'
  '[[ abc =~ x ]] ; slurp R3:$? ${#BASH_REMATCH[@]}'
  '[[ a.c =~ "a.c" ]] ; slurp R4:$? ; [[ abc =~ "a.c" ]] ; slurp R5:$? ; [[ abc =~ a"."c ]] ; slurp R6:$?'
  're="(" ; [[ abc =~ $re ]] ; slurp R7:$?'
  '[[ x =~ ^(a|b)?x$ ]] ; slurp R8:$? "[${BASH_REMATCH[1]}]" ${#BASH_REMATCH[@]}'
  '[[ foo =~ o+ && bar =~ ^b ]] ; slurp R9:$? ${BASH_REMATCH[0]}'
  '[[ "a b" =~ ^a\ b$ ]] ; slurp R10:$?'
  '[[ "2026-10-09" =~ ^([0-9]{4})-([0-9]{2})-([0-9]{2})$ ]] ; slurp R11:${BASH_REMATCH[3]}/${BASH_REMATCH[2]}/${BASH_REMATCH[1]}'
  '[[ aaa =~ a* ]] ; slurp R12:${BASH_REMATCH[0]} ; [[ xaaa =~ a* ]] ; slurp "R13:[${BASH_REMATCH[0]}]"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^R[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^R[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    [[ =~ ]] and BASH_REMATCH are bash's ($(wc -l < "$WORK/want") lines: groups, a regex in a variable, no match, quoted text, bad 2, an idle group, &&, \\ blank, {n}, leftmost)"
else echo "  FAIL  =~ differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

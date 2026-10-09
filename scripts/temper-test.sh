#!/usr/bin/env bash
# temper-test.sh - temper (sh's set) against bash's set (v0.60.115): -e in
# a recipe (and not inside a condition), +e, -u stopping a recipe (and
# ${x:-y} still fine), -x's trace (words quoted as bash quotes them), the
# trace of a recipe not leaking out, temper -- and temper W setting $1..
# and $#, bare temper listing a variable.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-temper.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'slurp T1:a\ntemper -e\nif spoiled ; then slurp no ; fi\nspoiled || slurp T1:or\nspoiled\nslurp T1:never\n' > "$H/r1.sh"
printf 'temper -e\ntemper +e\nspoiled\nslurp T2:ran\n' > "$H/r2.sh"
printf 'temper -u\nslurp "T3:${nope:-dflt}"\nslurp T3:$nope\nslurp T3:never\n' > "$H/r3.sh"
printf 'temper -x\nslurp T4:in\n' > "$H/r4.sh"
for f in r1.sh r2.sh r3.sh r4.sh; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
LINES=(
  'follow /r1.sh ; slurp T5:$?'
  'follow /r2.sh ; slurp T6:$?'
  'follow /r3.sh ; slurp T7:$?'
  'follow /r4.sh ; slurp T8:after'
  "temper -- a 'b c' d ; slurp T9:\$# \"T9:\$2\""
  'temper x y ; slurp T10:$1$2 ; temper -- ; slurp T11:$#'
  'zqv="a b" ; slurp "T12:$(temper | cook sift.elf zqv=)"'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
XL='temper -x ; slurp hi "a b" ; xq="p q" ; slurp $xq "it'"'"'s" ; temper +x'
keys+=("$XL" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^T[0-9]+:' "$WORK/log" > "$WORK/got"
tosh() { sed 's/temper/set/g; s/slurp/echo/g; s/spoiled/false/g; s/cook sift.elf zqv=/grep ^zqv=/g'; }
for f in r1 r2 r3 r4; do tosh < "$H/$f.sh" > "$H/$f.bash"; done
script=""
for l in "${LINES[@]}"; do b=$(printf '%s' "$l" | tosh); b=${b//follow \//bash $H\/}; b=${b//.sh/.bash}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^T[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
# the trace: bash's, its set and echo read back as temper and slurp
grep -E '^\+ ' "$WORK/log" | grep -v 'T4:' > "$WORK/got.x"
bash -c "$(printf '%s' "$XL" | tosh)" 2>&1 >/dev/null | sed 's/^+ set /+ temper /; s/^+ echo /+ slurp /' > "$WORK/want.x"
cmp -s "$WORK/got.x" "$WORK/want.x" || { echo "        the -x trace differs:"; diff "$WORK/want.x" "$WORK/got.x" | sed 's/^/          /'; bad=1; }
# r4's trace stays in r4: one + line for slurp T4:in, none after it
[ "$(grep -c '^+ slurp T4:in' "$WORK/log")" = 1 ] || { echo "        a recipe's -x did not trace it"; bad=1; }
grep -E '^\+ slurp T8' "$WORK/log" >/dev/null && { echo "        a recipe's -x leaked out"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    temper is bash's set ($(wc -l < "$WORK/want") lines: -e +e -u in recipes, -- and W for \$1.. \$#, the listing; the -x trace and quoting, kept to its recipe)" \
    || { echo "  FAIL  temper differs from set"; fail=1; }
exit $fail

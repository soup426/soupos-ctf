#!/usr/bin/env bash
# elif-test.sh - if ... elif ... else ... fi against bash (v0.60.74).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-elif.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/e.sh" <<'S'
if false
then echo S1:no
elif false
then echo S1:no2
elif true
then
  echo S1:yes
fi
echo S2:after
S
sed -e 's/\becho\b/slurp/g' -e 's/\bfalse\b/cook taste.elf 1 -eq 2/g' -e 's/\btrue\b/cook taste.elf 1 -eq 1/g' "$WORK/e.sh" > "$WORK/E.SH"
mcopy -i "$WORK/disk.img" "$WORK/E.SH" ::/e.sh
LINES=(
  'for n in 1 2 3 4 ; do if (( n == 1 )) ; then slurp E1:one ; elif (( n == 2 )) ; then slurp E1:two ; elif (( n == 3 )) ; then slurp E1:three ; else slurp E1:other ; fi ; done'
  'if cook taste.elf 1 -eq 2 ; then slurp no ; elif cook taste.elf 1 -eq 1 ; then slurp E2:second ; fi'
  'if cook taste.elf 1 -eq 2 ; then slurp no ; elif cook taste.elf 1 -eq 3 ; then slurp no ; fi ; slurp E3:$?'
  'x=5 ; if (( x < 3 )) ; then slurp E4:low ; elif (( x < 7 )) ; then if (( x == 5 )) ; then slurp E4:five ; else slurp E4:mid ; fi ; else slurp E4:high ; fi'
  'if (( 0 )) ; then slurp a ; elif (( 0 )) ; then slurp b ; else slurp E5:else ; fi'
  'if (( 1 )) ; then slurp E6:first ; elif (( 1 )) ; then slurp E6:no ; fi'
  'f() { if (( $1 > 0 )) ; then slurp E7:pos ; elif (( $1 < 0 )) ; then slurp E7:neg ; else slurp E7:zero ; fi ; } ; f 3 ; f -2 ; f 0'
  'if (( 0 )) ; then slurp no ; elif slurp E8:cond-runs ; (( 0 )) ; then slurp no ; else slurp E8:else ; fi'
  'follow -e /e.sh ; slurp S3:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[ES][0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook taste.elf/test}; b=${b//slurp/echo}; b=${b//follow -e \/e.sh/bash -e $WORK\/e.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[ES][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    elif does what bash's does ($(wc -l < "$WORK/want") lines: a chain, the first true taken, none taken (status 0), nested if, else, a function, a two-command condition, a script over lines under -e)"
else echo "  FAIL  elif differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

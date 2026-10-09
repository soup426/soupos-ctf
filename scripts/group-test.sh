#!/usr/bin/env bash
# group-test.sh - { ... ; } groups against bash (v0.60.75).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-group.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  '{ slurp G1:a ; slurp G1:b ; }'
  '{ x=5 ; y=6 ; } ; slurp G2:$x$y'
  '{ slurp one ; slurp two ; } > /g.txt ; cook spoon.elf /g.txt | while take l ; do slurp G3:$l ; done'
  '{ slurp b ; slurp a ; } | cook rack.elf | while take l ; do slurp G4:$l ; done'
  '{ cook taste.elf 1 -eq 2 ; } && slurp no || slurp G5:or'
  '{ slurp three ; } >> /g.txt ; { while take l ; do slurp G6:$l ; done ; } < /g.txt'
  'f() { { slurp G7:nested ; } ; } ; f'
  'if cook taste.elf 1 -eq 1 ; then { slurp G8:in-if ; } ; fi'
  'slurp x | { take v ; slurp G9:$v ; }'
  'for i in 1 2 ; do { slurp G10:$i ; } ; done'
  '{ slurp G11:last ; cook taste.elf 1 -eq 2 ; } ; slurp G12:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^G[0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook taste.elf/test}; b=${b//cook spoon.elf/cat}; b=${b//cook rack.elf/sort}; b=${b//slurp/echo}; b=${b//take/read}; b=${b//\/g.txt/$WORK\/g.txt}
    script+="$b"$'\n'
done
bash -c "$script" 2>/dev/null | grep -E '^G[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    { } groups do what bash's do ($(wc -l < "$WORK/want") lines: in this shell, > >> <, | PROG, && ||, after X |, in a function, an if and a loop, the last status)"
else echo "  FAIL  groups differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

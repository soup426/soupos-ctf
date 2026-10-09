#!/usr/bin/env bash
# bang-test.sh - ! COMMAND against bash (v0.60.60). With it, two things the
# first run of this test turned up: `slurp x | cook prog` said "No such
# program: cook" (pipe_into put a cook of its own in front), and a program
# that was not there left $? as it was; sh gives 127.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bang.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'echo E1:start\n! true\necho E2:$?\nfalse\necho E3:no\n' > "$WORK/e.sh"
sed -e 's/\becho\b/slurp/g' -e 's/\btrue\b/cook taste.elf 1 -eq 1/' -e 's/\bfalse\b/cook taste.elf 1 -eq 2/' "$WORK/e.sh" > "$WORK/E.SH"
mcopy -i "$WORK/disk.img" "$WORK/E.SH" ::/e.sh
LINES=(
  '! cook taste.elf 1 -eq 2 ; slurp N1:$?'
  '! cook taste.elf 1 -eq 1 ; slurp N2:$?'
  '! slurp hi | cook sift.elf x ; slurp N3:$?'
  'if ! cook taste.elf 2 -gt 3 ; then slurp N4:yes ; fi'
  'n=0 ; while ! cook taste.elf $n -ge 3 ; do n=$((n+1)) ; done ; slurp N5:$n'
  '! cook taste.elf 1 -eq 2 && slurp N6:and'
  '! cook taste.elf 1 -eq 1 || slurp N7:or'
  '! ; slurp N8:$?'
  '! ! cook taste.elf 1 -eq 2 ; slurp N9:$?'
  'f() { return 3 ; } ; ! f ; slurp N10:$?'
  'until ! cook taste.elf 1 -eq 1 ; do slurp N11:no ; break ; done ; slurp N12:$?'
  'follow -e /e.sh ; slurp E4:$?'
  'slurp hi | cook sift.elf x ; slurp N13:$?'
  'slurp hi | cook flip.elf | cook sift.elf ih ; slurp N14:$?'
  'cook nosuch.elf ; slurp N15:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[NE][0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do b=${l//cook taste.elf/test}; b=${b//cook sift.elf/grep}; b=${b//cook flip.elf/rev}; b=${b//cook nosuch.elf/nosuch_prog_x}; b=${b//slurp/echo}; b=${b//follow -e \/e.sh/bash -e $WORK\/e.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[NE][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    ! does what bash's does ($(wc -l < "$WORK/want") lines: both ways, a whole pipeline, if, while, until, && and ||, bare, twice, a function, follow -e runs on, | cook prog, cook a | cook b, 127)"
else echo "  FAIL  ! differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

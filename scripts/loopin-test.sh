#!/usr/bin/env bash
# loopin-test.sh - a program into a shell loop, against bash (v0.60.34).
#
# `cook spoon.elf F | while take line ; do ... ; done` runs in soupOS; bash
# runs `cat F | while read line; do ...; done` with lastpipe, which is what
# soupOS does (the loop runs in this shell, so what it sets stays set, as
# in ksh and zsh; plain bash would forget n). The L: lines must match.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-loopin.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
printf 'apple pie\nbanana\n\ncherry tart\n  spaced  out  \ndate' > "$WORK/host/F.TXT"   # a blank line; no final newline
: > "$WORK/host/E.TXT"
for f in F E; do mcopy -i "$WORK/disk.img" "$WORK/host/$f.TXT" "::/$f.TXT"; done
LINES=(
  'cook spoon.elf /F.TXT | while take line ; do slurp "L1:$line" ; done'
  'cook spoon.elf /F.TXT | while take a b ; do slurp L2:$a:$b ; done'
  'cook spoon.elf /F.TXT | if take first ; then slurp L3:$first ; fi'
  'n=0 ; cook tally.elf 500 | while take x ; do n=$((n+1)) ; done ; slurp L4:$n'
  'slurp one two | while take a ; do slurp L5:$a ; done'
  'cook spoon.elf /E.TXT | while take x ; do slurp L6:never ; done ; slurp L6:after:$?'
  'cook tally.elf 100000 | if take x ; then slurp L7:$x ; fi ; slurp L7:done'
  'cook spoon.elf /F.TXT | while take line ; do case $line in a*) slurp L8:A ;; *) slurp L8:other ;; esac ; done'
  'n=0 ; until cook taste.elf $n -eq 3 ; do slurp $n ; n=$((n+1)) ; done | stack.elf | while take x ; do slurp L9:$x ; done'
  'for x in b a c ; do slurp $x ; done | rack.elf | while take y ; do slurp L9:$y ; done'
  'if cook taste.elf 1 = 1 ; then slurp yes ; fi | raise.elf | case z in z) take w ; slurp L9:$w ;; esac'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("cook mise.elf after-loops" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^L[0-9]:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook spoon.elf \//cat }; b=${b//cook tally.elf/seq}; b=${b//take/read}; b=${b//slurp/echo}
    b=${b//cook taste.elf/test}; b=${b//stack.elf/tac}; b=${b//rack.elf/sort}; b=${b//raise.elf/tr a-z A-Z}
    script+="$b"$'\n'
done
( cd "$WORK/host" && bash -O lastpipe -c "$script" ) | grep -E '^L[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then
    echo "  ok    all ${#LINES[@]} lines print what bash (lastpipe) prints ($(wc -l < "$WORK/want") lines: a file, two words, if, a count kept, a builtin, empty, a reader that stops at one of 100000, case inside, a loop | prog | a loop)"
else echo "  FAIL  a program into a loop differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
grep -q 'TESTOUT args=after-loops' "$WORK/serial.log" && echo "  ok    programs still run after all that" \
    || { echo "  FAIL  a program did not run after the loops"; fail=1; }
exit $fail

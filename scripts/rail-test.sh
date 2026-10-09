#!/usr/bin/env bash
# rail-test.sh - rail (bash's jobs) against bash's jobs under set -m
# (v0.60.116): two running jobs, one a pipeline, a finished one shown once
# as Exit 42 and then gone, -p, a killed one, rail %N and %9 (status 1),
# and a Ctrl-Z'd one as Stopped. The two jobs sleep long enough to outlast
# the typing of the next lines (a line of 90 keys takes seconds), and greet
# gets two seconds to have exited before the first listing: under a loaded
# full check (2026-10-09, twice) half a second was not enough for it, and 13
# seconds ran out before the last lines were typed. marinate.elf is sleep, spoon.elf cat and
# greet.elf exits 42.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-rail.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'cook marinate.elf 30 & cook marinate.elf 31 | cook spoon.elf & cook greet.elf & cook marinate.elf 2 ; rail ; rail'
  'n=$(rail -p | cook weigh.elf -l) ; slurp R1:$((n)) ; kill %1 ; cook marinate.elf 0.3 ; rail'
  'rail %2 ; rail %9 ; slurp R2:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('kill %2 ; rest' "UNTIL:@soupOS:" 'cook marinate.elf 6' "WAIT:2" "KEY:ctrl-z" "UNTIL:@soupOS:"
       'rail' "UNTIL:@soupOS:" 'kill %1' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^(\[[0-9]+\][-+ ]  |R[0-9]:)' "$WORK/log" \
  | sed "s/cook marinate.elf/sleep/g; s/cook spoon.elf/cat/g; s/cook greet.elf/sh -c 'exit 42'/g" > "$WORK/got"
script='set -m'$'\n'
for l in "${LINES[@]}"; do
  b=${l//cook marinate.elf/sleep}; b=${b//cook spoon.elf/cat}; b=${b//cook greet.elf/sh -c \'exit 42\'}
  b=${b//cook weigh.elf -l/wc -l}; b=${b//rail/jobs}; b=${b//kill %1/kill -9 %1}; b=${b//slurp/echo}
  script+="$b"$'\n'
done
script+='kill %2'$'\n'
bash -c "$script" 2>/dev/null > "$WORK/want.all"
grep -E '^(\[[0-9]+\][-+ ]  |R[0-9]:)' "$WORK/want.all" > "$WORK/want"
# Ctrl-Z: bash -c cannot be stopped from a keyboard, so the line is written out
printf '[1]+  %-27s%s\n' Stopped 'sleep 6' >> "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    rail is bash's jobs ($(wc -l < "$WORK/want") lines: running, a pipeline, Exit 42 once, -p, killed, %N, %9 status 1, stopped)"
else echo "  FAIL  rail differs from jobs:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

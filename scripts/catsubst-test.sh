#!/usr/bin/env bash
# catsubst-test.sh - $(< FILE) against bash (v0.60.86): the file's text,
# no program started, split as any $( ) is.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-catsubst.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/b"
printf 'l1\nl2\n\n' > "$WORK/b/f.txt"; printf 'one two' > "$WORK/b/g.txt"; printf '21\n' > "$WORK/b/n.txt"
for f in f g n; do mcopy -i "$WORK/disk.img" "$WORK/b/$f.txt" "::/$f.txt"; done
LINES=(
  'slurp D1:$(< /f.txt)'
  'for w in $(< /g.txt) ; do slurp D2:$w ; done'
  'slurp D3:[$(</g.txt)]'
  'f=/g.txt ; slurp D4:$(< $f)'
  'slurp D5:[$(< /nope.txt)]:$?'
  'cook dish.elf -v z "%s|" "$(< /g.txt)" ; slurp D6:$z'
  'n=$(< /n.txt) ; slurp D7:$((n * 2))'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^D[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook dish.elf/printf}; b=${b//slurp/echo}; b=${b//\//$WORK\/b\/}; script+="$b"$'\n'; done
( cd "$WORK/b" && bash -c "$script" ) 2>/dev/null | grep -E '^D[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    \$(< FILE) gives what bash's does ($(wc -l < "$WORK/want") lines: in a word, for over it, no space, \$f, a file not there and its status, a program's argument, arithmetic)"
else echo "  FAIL  \$(< FILE) differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
spawned=$(tr -d '\r' < "$WORK/serial.log" | grep -c 'spawned /spoon.elf')
[ "$spawned" = 0 ] && echo "  ok    no program was started for them" || { echo "  FAIL  spoon.elf was started $spawned times"; fail=1; }
exit $fail

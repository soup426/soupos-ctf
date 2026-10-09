#!/usr/bin/env bash
# nickname-test.sh - nickname and forget (sh's alias and unalias) against
# bash with expand_aliases (v0.60.90). bash expands a line's aliases when
# it reads the line, so `unalias hi ; hi` still runs hi; soupOS takes each
# command as it runs it, so forget is on a line of its own here.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-nickname.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  "nickname hi='slurp N1:hello'"
  'hi'
  'hi there ; hi again'
  "nickname two='slurp N2:a ; slurp N2:b'"
  'two'
  "nickname sl='slurp N3:x | cook spoon.elf'"
  'sl'
  "nickname slurp='slurp N4:'"
  'slurp self'
  'forget slurp'
  'slurp N5:plain'
  'nickname | while take -r l ; do slurp "N6:$l" ; done'
  'nickname hi ; nickname nope ; slurp N7:$?'
  'inspect -t hi | while take l ; do slurp N8:$l ; done'
  'forget hi'
  'hi ; slurp N9:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^N[0-9]+:|^nickname ' | sed -e 's/^nickname /alias /' -e 's/^N6:nickname /N6:alias /' -e 's/slurp/echo/g' -e 's/cook spoon\.elf/cat/g' > "$WORK/got"
script=$'shopt -s expand_aliases\n'
for l in "${LINES[@]}"; do b=${l//nickname/alias}; b=${b//forget/unalias}; b=${b//slurp/echo}; b=${b//cook spoon.elf/cat}; b=${b//take/read}; b=${b//inspect/type}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^N[0-9]+:|^alias ' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    nicknames do what bash's aliases do ($(wc -l < "$WORK/want") lines: the first word and its arguments, ; and | in the text, one inside itself, forget, the list in order, one shown, not found, inspect, forgotten)"
else echo "  FAIL  nicknames differ from bash's aliases:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# wordsplit-test.sh - slurp splits words as echo does, against bash (v0.60.38).
#
# Unquoted blanks, literal or from an expansion, separate words and print as
# one space; quoted stretches keep theirs; '' is a word. Programs (call.elf)
# and for already split this way; they are here so they stay that way.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-wordsplit.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp W1:a    b'
  'v="x   y" ; slurp W2:$v'
  'slurp "W3:a   b"'
  'v="x   y" ; slurp "W4:$v"'
  'v= ; slurp W5:a $v b'
  'v="  lead" ; slurp W6:$v'
  'v="x   y" ; cook call.elf W7:$v'
  'v="p   q" ; for w in $v ; do slurp W8:$w ; done'
  "slurp W9: '' end"
  "slurp W10:'x  y'   z"
  'slurp    W11:leading    and   trailing   '
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^W[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//cook call.elf/echo}; script+="$b"$'\n'; done
bash -c "$script" | grep -E '^W[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints (runs of blanks, from text and from \$v, quoted kept, '' a word, a program, for)"
else echo "  FAIL  word splitting differs from bash (· is a space):"; diff <(sed 's/ /·/g' "$WORK/want") <(sed 's/ /·/g' "$WORK/got") | sed 's/^/        /'; fail=1; fi
exit $fail

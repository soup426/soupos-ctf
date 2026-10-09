#!/usr/bin/env bash
# plain-test.sh - plain (sh's command) against bash's command (v0.60.100):
# -v for a builtin, a program, a nickname, a function, a keyword and none;
# a program run through it; and a function wrapping a builtin of its own
# name with plain, plain going past that function.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-plain.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'plain -v slurp cd > /v1.txt'
  'plain -v nosuch ; slurp P5:$?'
  'plain -v spoon.elf > /v2.txt'
  "nickname ll='slurp nick'"
  'plain -v ll > /v3.txt'
  'f() { slurp in-f ; } ; plain -v f if > /v4.txt'
  'slurp P10:prog > /t.txt ; plain spoon.elf /t.txt'
  'plain nosuch ; slurp P11:$?'
  'slurp() { plain slurp "[$@]" ; } ; slurp P12:wrapped'
  'plain slurp P13:direct'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
B="$WORK/b"; mkdir -p "$B"
script=$'shopt -s expand_aliases\n'
for l in "${LINES[@]}"; do
    b=${l//plain/command}; b=${b//slurp/echo}; b=${b//nickname/alias}; b=${b//spoon.elf/cat}
    b=${b//\/v/$B\/v}; b=${b//\/t.txt/$B\/t.txt}
    script+="$b"$'\n'
done
env -i PATH="$PATH" bash -c "$script" 2>/dev/null | grep -E '^\[?P[0-9]+:' > "$WORK/want"
tr -d '\r' < "$WORK/serial.log" | grep -E '^\[?P[0-9]+:' > "$WORK/got"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { echo "        printed:"; diff "$WORK/want" "$WORK/got" | sed 's/^/          /'; bad=1; }
cat_path=$(command -v cat)
for k in 1 2 3 4; do
    mcopy -n -i "$WORK/disk.img" "::/v$k.txt" "$WORK/got$k" 2>/dev/null || { echo "        v$k.txt was not written"; bad=1; continue; }
    sed -e 's/^echo$/slurp/' -e "s#^$cat_path\$#/spoon.elf#" -e "s/^alias ll='echo nick'\$/nickname ll='slurp nick'/" "$B/v$k.txt" > "$WORK/want$k"
    cmp -s "$WORK/got$k" "$WORK/want$k" || { echo "        v$k.txt differs:"; diff "$WORK/want$k" "$WORK/got$k" | sed 's/^/          /'; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    plain is bash's command (4 files and 5 printed lines: -v builtins, none, a program, a nickname, a function and a keyword; a program run, none 127, a wrapping function, past it)" \
    || { echo "  FAIL  plain differs from bash's command"; fail=1; }
exit $fail

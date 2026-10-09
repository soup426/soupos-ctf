#!/usr/bin/env bash
# trim-test.sh - ${NAME#pat} ${NAME##pat} ${NAME%pat} ${NAME%%pat} against bash (v0.60.64).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-trim.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'p=/home/soup/file.tar.gz ; slurp T1:${p#*/}:${p##*/}'
  'slurp T2:${p%.*}:${p%%.*}'
  'slurp T3:${p#nomatch}:${p%/*}'
  'e=gz ; slurp T4:${p%.$e}:${p##*[/.]}'
  'slurp T5:${u#x}:${u%%*}:end'
  'slurp T6:${p#}:${p##*}:${p#*}'
  'v="a b c" ; slurp "T7:${v% *}" "${v#* }"'
  'f() { slurp T8:${1%.txt}:${#1} ; } ; f notes.txt'
  'slurp T9:${p%?}:${p#?}'
  'w=aaa ; slurp T10:${w#a}:${w##a}:${w%a*}:${w%%a*}'
  'for x in one.c two.h ; do slurp T11:${x%.[ch]} ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^T[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^T[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    trimming does what bash's does ($(wc -l < "$WORK/want") lines: # ## % %%, no match, \$X and [..] in the pattern, unset, empty pattern, quoted, \$1, ?, a loop)"
else echo "  FAIL  trimming differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

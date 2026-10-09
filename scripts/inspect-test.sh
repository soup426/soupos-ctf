#!/usr/bin/env bash
# inspect-test.sh - inspect (sh's type) against bash's type (v0.60.72).
#
# The kinds, with -t, against type -t: a keyword, a function, a builtin
# (slurp for echo), a program (spoon.elf for cat), and not found; the
# statuses; and the long form for a builtin and a program, against type
# with the names swapped back.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-inspect.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'f() { slurp hi ; } ; inspect -t if f slurp spoon.elf while break | while take k ; do slurp K:$k ; done'
  'inspect -t nosuch ; slurp K:status:$?'
  'inspect slurp ; inspect spoon.elf ; slurp K:status:$?'
  'inspect slurp nosuch ; slurp K:status:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^K:|^(slurp|spoon\.elf) is ' > "$WORK/got"
cat_path=$(type -P cat)
script=""
for l in "${LINES[@]}"; do
    b=${l//inspect/type}; b=${b//slurp hi/echo hi}; b=${b//slurp K/echo K}; b=${b//take/read}
    b=${b//type -t if f slurp spoon.elf/type -t if f echo cat}; b=${b//type slurp/type echo}; b=${b//type spoon.elf/type cat}
    script+="$b"$'\n'
done
bash -O lastpipe -c "$script" 2>/dev/null | sed -e 's/^echo is /slurp is /' -e "s#^cat is $cat_path\$#spoon.elf is /spoon.elf#" \
    | grep -E '^K:|^(slurp|spoon\.elf) is ' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    inspect says what bash's type says ($(wc -l < "$WORK/want") lines: keyword, function, builtin, file, a special builtin, not found, statuses, the long form)"
else echo "  FAIL  inspect differs from bash's type:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

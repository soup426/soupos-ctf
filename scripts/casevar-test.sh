#!/usr/bin/env bash
# casevar-test.sh - ${NAME^} ^^ , ,, (with a pattern) and ${!NAME}, against bash (v0.60.85).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-casevar.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'v=hello ; slurp C1:${v^}:${v^^}:${v,}'
  'w=HeLLo ; slurp C2:${w,,}:${w,}:${w^^}'
  'x="mixed Case" ; slurp "C3:${x^^}:${x,,}:${x^}"'
  'v=hello ; slurp C4:${v^^[le]}:${v^^l}:${v^[a-g]}:${v^[p-z]}'
  'n=v ; slurp C5:${!n}'
  'p=missing ; slurp C6:[${!p}]'
  'f() { k=2 ; slurp C7:${!k} ; } ; f a b c'
  'slurp C8:${u2^^}:end'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^C[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^C[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    case and indirection are bash's ($(wc -l < "$WORK/want") lines: ^ ^^ , ,, both ways, with a space, with a pattern, \${!NAME} set and unset and to \$2, unset)"
else echo "  FAIL  case or indirection differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# keys-test.sh - ${!a[@]} (an array's indexes) and ${!prefix*} ${!prefix@}
# (the variable names starting so), against bash (v0.60.98).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-keys.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'a=(x y z) ; slurp K1:${!a[@]}:${!a[*]}'
  'for k in "${!a[@]}" ; do slurp "K2:$k=${a[k]}" ; done'
  'b=() ; slurp K3:[${!b[@]}]'
  'pfc=3 ; pfa=1 ; pfb=2 ; slurp K4:${!pf*}'
  'for n in "${!pf@}" ; do slurp K5:$n ; done'
  'slurp "K6:${!pf*}"'
  'slurp K7:[${!zz*}]'
  'v=one ; slurp K8:${!v[@]}'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^K[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
env -i PATH="$PATH" bash -c "$script" 2>/dev/null | grep -E '^K[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    indexes and names are bash's ($(wc -l < "$WORK/want") lines: \${!a[@]} \${!a[*]}, for over them, empty, \${!pf*} by name, \"\${!pf@}\" a word each, \"\${!pf*}\" one, none, a plain variable's 0)"
else echo "  FAIL  indexes or names differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# brace-test.sh - brace expansion against bash (v0.60.89).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-brace.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp B1:{a,b,c}'
  'slurp B2:x{a,b}y {1..3}'
  'slurp B3 {5..1} {a..e} {e..a}'
  'slurp B4 {01..10} {1..10..3} {10..1..4} {-2..2}'
  'slurp B5 a{b,c{d,e}}f {x,y}{1,2}'
  'slurp B6 "{a,b}" {a} {} \{x,y\} {a..} {1..b}'
  'for i in {1..3} ; do slurp B7:$i ; done'
  'v=x ; slurp B8 {$v,y} pre{,post}'
  'a={1,2} ; slurp B9:$a'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^B[0-9]+' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^B[0-9]+' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    braces expand as bash's ($(wc -l < "$WORK/want") lines: lists with a prefix and suffix, numbers and letters both ways, padding, steps, negatives, nested, one after another, left alone when quoted or not braces, a for list, \$v after them, an assignment's kept)"
else echo "  FAIL  brace expansion differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

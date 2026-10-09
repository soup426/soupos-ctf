#!/usr/bin/env bash
# paramsub-test.sh - ${NAME/pat/rep} (//, /#, /%) and ${NAME:off:len} against bash (v0.60.65).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-paramsub.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'v=abc ; slurp R1:${v/#/X}:${v/%/X}:${v//b*/X}:${v//}:${v/}'
  'slurp R2:${v//?/.}:${v/b}:${v/#a/Z}:${v/%c/Z}:${v/#b/Z}'
  'w=hello ; slurp R3:${w//l/L}:${w/l/L}:${w//[eo]/_}:${w/l*o/X}'
  'p=a/b/c ; slurp R4:${p//\//-}:${p/\//-}'
  'e= ; slurp R5:[${e//x/y}]:[${u/x/y}]'
  'r=Q ; slurp R6:${w//l/$r}:${w/h/${r}${r}}'
  'w=hello ; slurp S1:${w:1}:${w:1:3}:${w: -2}:${w:1:-1}:[${w:9}]'
  'slurp S2:${w:(-3):2}:[${w:0:0}]:${w:2+1}'
  'n=2 ; slurp S3:${w:n}:${w:$n:n}:${w:0:n*2}'
  'f() { slurp S4:${1:0:3}:${1//o/0} ; } ; f foobar'
  's="a b c" ; slurp "S5:${s// /_}" "${s:2}"'
  'for x in one two ; do slurp S6:${x:0:1}${x/o/O} ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[RS][0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[RS][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    replacing and slicing do what bash's do ($(wc -l < "$WORK/want") lines: / // /# /%, empty and anchored patterns, \\/, \$X in the replacement, offsets and lengths negative and from arithmetic, \$1, quoted, a loop)"
else echo "  FAIL  replacing or slicing differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

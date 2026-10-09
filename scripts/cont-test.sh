#!/usr/bin/env bash
# cont-test.sh - an unfinished if, for, while, case, { or function typed at
# the prompt asks for the rest with > until it closes, then runs (v0.60.81),
# as does a line ending in && || or |. Ctrl-C drops it (130), Ctrl-D is "unexpected end of file" (2), and the
# whole joined line is what the history keeps (the up arrow runs it again).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cont.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'for x in a b' 'do' 'slurp C1:$x' 'done' "UNTIL:@soupOS:"
  '[ 1 = 1 ] &&' 'slurp C0:continued' "UNTIL:@soupOS:"
  'slurp C9:x |' 'cook spoon.elf' "UNTIL:@soupOS:"
  'if [ 1 = 1 ]' 'then slurp C2:yes' 'else slurp no' 'fi' "UNTIL:@soupOS:"
  'f() {' 'slurp C3:in-f' '}' "UNTIL:@soupOS:" 'f' "UNTIL:@soupOS:"
  'while [ 0 = 1 ] ; do' "KEY:ctrl-c" "UNTIL:@soupOS:" 'slurp C4:$?' "UNTIL:@soupOS:"
  'for x in 1' "KEY:ctrl-d" "UNTIL:@soupOS:" 'slurp C5:$?' "UNTIL:@soupOS:"
  'case b in' 'a) slurp no ;;' 'b) slurp C6:b ;;' 'esac' "UNTIL:@soupOS:"
  'for i in 1 2' 'do' 'if [ $i = 2 ]' 'then slurp C7:$i' 'fi' 'done' "UNTIL:@soupOS:"
  '{ slurp C8:a' 'slurp C8:b ; }' "UNTIL:@soupOS:"
  "KEY:up" "KEY:ret" "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^C[0-9]+:' > "$WORK/got"
printf '%s\n' C1:a C1:b C0:continued C9:x C2:yes C3:in-f C4:130 C5:2 C6:b C7:2 C8:a C8:b C8:a C8:b > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    the prompt waits for the rest ($(wc -l < "$WORK/want") lines: for, a line ending in && and in |, if/else, a function, Ctrl-C 130, Ctrl-D 2, case, a nested if in a for, a { } group, the up arrow runs the whole again)"
else echo "  FAIL  continuation:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
if tr -d '\r' < "$WORK/serial.log" | grep -F 'syntax error: unexpected end of file' >/dev/null; then echo "  ok    Ctrl-D says the end came too soon"
else echo "  FAIL  no word on Ctrl-D"; fail=1; fi
exit $fail

#!/usr/bin/env bash
# histexp-test.sh - history expansion typed at the prompt (v0.60.93): !!,
# !$, !word, !-N shown and run; an event not there runs nothing; and the
# !s that are not history left alone (!=, '...', "hi!", ${!n}, \!, ! cmd).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-histexp.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'slurp H1:one' "UNTIL:@soupOS:"
  '!!' "UNTIL:@soupOS:"
  'slurp H2:two last' "UNTIL:@soupOS:"
  'slurp H3:!$' "UNTIL:@soupOS:"
  '!slurp H2' "UNTIL:@soupOS:"
  '!-3' "UNTIL:@soupOS:"
  '!nosuch ; slurp H9:ran' "UNTIL:@soupOS:"
  '[ 1 != 2 ] && slurp H5:noteq' "UNTIL:@soupOS:"
  "slurp 'H6:!!' \"H7:hi!\"" "UNTIL:@soupOS:"
  'v=x ; n=v ; slurp H8:${!n} \!kept' "UNTIL:@soupOS:"
  '! [ 1 = 2 ] && slurp H10:bang' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^H[0-9]+:' > "$WORK/got"
# !slurp is the last line starting "slurp" (H3's) with " H2" after it, as
# bash's !word stops at the blank; !-3 is then the H2 line.
printf '%s\n' 'H1:one' 'H1:one' 'H2:two last' 'H3:last' 'H3:last H2' 'H2:two last' 'H5:noteq' 'H6:!! H7:hi!' 'H8:x !kept' 'H10:bang' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    history expands as bash's at its prompt ($(wc -l < "$WORK/want") lines: !!, !\$, !word, !-3, not found runs nothing, != '!!' \"hi!\" \${!n} \\! and ! cmd left alone)"
else echo "  FAIL  history expansion:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
L=$(tr -d '\r' < "$WORK/serial.log")
if echo "$L" | grep -x 'slurp H3:last' >/dev/null && echo "$L" | grep -F '!nosuch: event not found' >/dev/null; then echo "  ok    the line is shown as it runs, and a missing event is said"
else echo "  FAIL  no shown line or no 'event not found'"; fail=1; fi
exit $fail

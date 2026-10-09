#!/usr/bin/env bash
# encore-test.sh - encore.elf (yes) against the host's yes (v0.60.108): y,
# or its words, until the reader goes; the pipeline ends and nothing of
# encore is left running.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-encore.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'cook encore.elf | cook skim.elf -n 3 > /y1.txt' "UNTIL:@soupOS:"
  'cook encore.elf more soup | cook skim.elf -n 2 > /y2.txt' "UNTIL:@soupOS:"
  'cook encore.elf | cook skim.elf -n 500 | cook weigh.elf -l > /y3.txt' "UNTIL:@soupOS:" "WAIT:1"
  'orders | while take -r l ; do slurp "O:$l" ; done' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
get() { mcopy -n -i "$WORK/disk.img" "::/$1" "$WORK/g.$1" 2>/dev/null || { echo "        $1 was not written"; bad=1; return 1; }; }
# what yes | head -n N gives, written out: under pipefail yes's SIGPIPE
# would fail the comparison itself
printf 'y\ny\ny\n' > "$WORK/w.y1"; printf 'more soup\nmore soup\n' > "$WORK/w.y2"
get y1.txt && { cmp -s "$WORK/w.y1" "$WORK/g.y1.txt" || { echo "        y1 differs from yes | head -n 3"; bad=1; }; }
get y2.txt && { cmp -s "$WORK/w.y2" "$WORK/g.y2.txt" || { echo "        y2 differs"; bad=1; }; }
get y3.txt && { [ "$(tr -d ' ' < "$WORK/g.y3.txt")" = 500 ] || { echo "        500 lines counted as '$(cat "$WORK/g.y3.txt")'"; bad=1; }; }
tr -d '\r' < "$WORK/serial.log" | grep -E '^O:.*encore\.elf' | grep -E ' R ' >/dev/null && { echo "        an encore is still running"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    encore is the host's yes: y and its words until the reader goes (3, 2 and 500 lines), and none is left running" \
    || { echo "  FAIL  encore differs from yes"; fail=1; }
exit $fail

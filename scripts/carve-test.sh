#!/usr/bin/env bash
# carve-test.sh - carve.elf (split) against GNU split, piece by piece and
# byte for byte (v0.60.123): -l N, the default 1000 lines and x, -b N and
# -b 1K on binary bytes, a last line without a newline, stdin as -, and
# nothing in making no pieces.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-carve.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
seq -f 'line %g' 25 > "$H/l.txt"; seq 2500 > "$H/big.txt"
head -c 350 /dev/urandom > "$H/b.bin"; head -c 3000 /dev/urandom > "$H/k.bin"
printf 'a\nb\nc\nd\ne' > "$H/n.txt"; : > "$H/e.txt"
for f in l.txt big.txt b.bin k.bin n.txt e.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
keys=("headchef" "rosemary" "WAIT:1"
  'for d in c1 c2 c3 c4 c5 c6 c7 ; do mkbowl /$d ; done' "UNTIL:@soupOS:"
  'cook carve.elf -l 10 /l.txt /c1/p ; cook carve.elf /big.txt /c2/x' "UNTIL:@soupOS:"
  'cook carve.elf -b 100 /b.bin /c3/b ; cook carve.elf -b 1K /k.bin /c4/k' "UNTIL:@soupOS:"
  'cook carve.elf -l 2 /n.txt /c5/n ; cook spoon.elf /l.txt | cook carve.elf -l 7 - /c6/s' "UNTIL:@soupOS:"
  'cook carve.elf /e.txt /c7/e ; slurp E:$?' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
G="$WORK/g"; W="$WORK/w"; mkdir -p "$G" "$W"
for d in c1 c2 c3 c4 c5 c6 c7; do mkdir -p "$G/$d" "$W/$d"; mcopy -s -n -i "$WORK/disk.img" "::/$d/*" "$G/$d/" >/dev/null 2>&1; done
(cd "$H" && split -l 10 l.txt "$W/c1/p" && split big.txt "$W/c2/x" && split -b 100 b.bin "$W/c3/b" && split -b 1K k.bin "$W/c4/k" \
   && split -l 2 n.txt "$W/c5/n" && split -l 7 - "$W/c6/s" < l.txt && split e.txt "$W/c7/e")
bad=0; pieces=0
for d in c1 c2 c3 c4 c5 c6 c7; do
  w=$(cd "$W/$d" && ls | tr '\n' ' '); g=$(cd "$G/$d" && ls | tr '\n' ' ')
  [ "$w" = "$g" ] || { echo "        $d: pieces '$g', split made '$w'"; bad=1; continue; }
  for f in $w; do pieces=$((pieces+1)); cmp -s "$W/$d/$f" "$G/$d/$f" || { echo "        $d/$f differs"; bad=1; }; done
done
tr -d '\r' < "$WORK/serial.log" | grep -x 'E:0' >/dev/null || { echo "        nothing in was not status 0"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    carve is GNU split ($pieces pieces alike: -l, 1000 and x, -b, -b 1K, binary, no last newline, stdin, nothing in nothing out)" \
    || { echo "  FAIL  carve differs from split"; fail=1; }
exit $fail

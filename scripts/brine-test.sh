#!/usr/bin/env bash
# brine-test.sh - brine.elf (base64) against the host's base64 (v0.60.106):
# text over a line, every byte value, empty, one and two bytes (the
# padding), from a pipe; decoding the host's, and bad input status 1.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-brine.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
for k in 1 2 3; do printf 'hello soupOS, line %d of the brine test\n' $k; done > "$H/t.txt"
python3 -c "import sys; sys.stdout.buffer.write(bytes(range(256)) + b'\x00')" > "$H/bin.dat"
: > "$H/e.txt"; printf a > "$H/one.txt"; printf ab > "$H/two.txt"
base64 "$H/bin.dat" > "$H/hb.b64"; printf 'abc$\n' > "$H/bad.b64"
for f in t.txt bin.dat e.txt one.txt two.txt hb.b64 bad.b64; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
keys=("headchef" "rosemary" "WAIT:1")
for f in t bin e one two; do
    ext=$([ $f = bin ] && echo dat || echo txt)
    keys+=("cook brine.elf /$f.$ext > /$f.b64" "UNTIL:@soupOS:" "cook brine.elf -d /$f.b64 > /$f.dec" "UNTIL:@soupOS:")
done
keys+=("cook brine.elf -d /hb.b64 > /hb.dec" "UNTIL:@soupOS:"
       "cook spoon.elf /two.txt | cook brine.elf > /p.b64" "UNTIL:@soupOS:"
       'cook brine.elf -d /bad.b64 ; slurp S:$?' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
get() { mcopy -n -i "$WORK/disk.img" "::/$1" "$WORK/g.$1" 2>/dev/null || { echo "        $1 was not written"; bad=1; return 1; }; }
for f in t bin e one two; do
    ext=$([ $f = bin ] && echo dat || echo txt)
    get $f.b64 && { base64 "$H/$f.$ext" | cmp -s - "$WORK/g.$f.b64" || { echo "        $f.b64 differs from base64's"; bad=1; }; }
    get $f.dec && { cmp -s "$WORK/g.$f.dec" "$H/$f.$ext" || { echo "        $f decoded is not $f.$ext"; bad=1; }; }
done
get hb.dec && { cmp -s "$WORK/g.hb.dec" "$H/bin.dat" || { echo "        the host's base64 decoded wrong"; bad=1; }; }
get p.b64 && { base64 "$H/two.txt" | cmp -s - "$WORK/g.p.b64" || { echo "        from a pipe differs"; bad=1; }; }
tr -d '\r' < "$WORK/serial.log" | grep -x 'S:1' >/dev/null || { echo "        bad input was not status 1"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    brine is the host's base64 both ways, byte for byte (text over a line, 257 bytes of every value, empty, = and ==, a pipe, the host's decoded, bad input 1)" \
    || { echo "  FAIL  brine differs from base64"; fail=1; }
exit $fail

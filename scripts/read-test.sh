#!/usr/bin/env bash
# read-test.sh - take NAME... (sh's read), against sh (v0.60.4).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-read.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/R.TXT" <<'SCRIPT'
take A
cook call.elf got=$A > /R1.TXT
take X Y
cook call.elf x=$X y=$Y > /R2.TXT
cook call.elf $(take Q ; cook call.elf st=$?) > /R3.TXT
SCRIPT
mcopy -i "$WORK/disk.img" "$WORK/R.TXT" ::/R.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /R.TXT" "WAIT:2" \
    "hello  world" "WAIT:2" "one two three" "WAIT:3" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; ( cd "$WORK/host" && sed -e 's|cook call.elf|echo|g' -e 's| /R| R|g' -e 's|take |read |g' "$WORK/R.TXT" > r.sh \
    && printf 'hello  world\none two three\n' | sh r.sh ) >/dev/null 2>&1
bad=0
for n in 1 2 3; do
    g=$(mcopy -n -i "$WORK/disk.img" "::/R$n.TXT" - 2>/dev/null); w=$(cat "$WORK/host/R$n.TXT" 2>/dev/null)
    [ "$g" = "$w" ] || { echo "        R$n: [$g] sh [$w]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    take matches sh's read: a whole line, words with the last taking the rest, end of input status 1" \
    || { echo "  FAIL  read differs from sh"; fail=1; }
exit $fail

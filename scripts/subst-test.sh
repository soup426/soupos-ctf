#!/usr/bin/env bash
# subst-test.sh - $(COMMAND), against sh (v0.60.3).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-subst.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
seq 1 5 > "$WORK/D.TXT"
cat > "$WORK/S.TXT" <<'SCRIPT'
cook call.elf [$(cook call.elf hi)] > /S1.TXT
N=$(cook tally.elf 3) ; cook call.elf n=$N > /S2.TXT
for x in $(cook tally.elf 3) ; do cook call.elf x$x >> /S3.TXT ; done
C=$(cook weigh.elf -l < /D.TXT) ; cook call.elf lines=$C > /S4.TXT
E=$(cook call.elf) ; cook call.elf [$E] > /S5.TXT
cook call.elf $(cook tally.elf 3 | rack.elf -r) > /S6.TXT
cook call.elf '$(no)' > /S7.TXT
cook taste.elf 1 = 2 ; S=$(cook call.elf x) ; cook call.elf $? > /S8.TXT
SCRIPT
for f in S D; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /S.TXT" "UNTIL:@soupOS:" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; cp "$WORK/D.TXT" "$WORK/host/"
( cd "$WORK/host" && sed -e 's|cook call.elf|echo|g' -e 's|cook tally.elf|seq|g' -e 's|cook weigh.elf|wc|g' -e 's|rack.elf|LC_ALL=C sort|g' \
    -e 's|cook taste.elf|test|g' -e 's| /S| S|g' -e 's| /D| D|g' "$WORK/S.TXT" > s.sh && sh s.sh ) >/dev/null 2>&1
bad=0
for n in 1 2 3 4 5 6 7 8; do
    g=$(mcopy -n -i "$WORK/disk.img" "::/S$n.TXT" - 2>/dev/null); w=$(cat "$WORK/host/S$n.TXT" 2>/dev/null)
    [ "$g" = "$w" ] || { echo "        S$n: [$(echo "$g" | tr '\n' '|')] sh [$(echo "$w" | tr '\n' '|')]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    eight lines match sh: in an argument, an assignment kept whole, for over it, wc -l < F, empty, a pipeline, '\$(...)' kept, the status after" \
    || { echo "  FAIL  command substitution differs from sh"; fail=1; }
exit $fail

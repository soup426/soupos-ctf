#!/usr/bin/env bash
# while-test.sh - while loops against sh, and Ctrl-C stopping a loop (v0.60.1).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-while.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/W.TXT" <<'SCRIPT'
N=0 ; while cook taste.elf $N -lt 5 ; do cook call.elf n=$N >> /W1.TXT ; N=$((N+1)) ; done ; cook call.elf $? > /W2.TXT
while cook taste.elf 1 = 2 ; do cook call.elf x > /W3.TXT ; done ; cook call.elf $? > /W4.TXT
I=0 ; while cook taste.elf $I -lt 2 ; do for c in a b ; do cook call.elf $I$c >> /W5.TXT ; done ; I=$((I+1)) ; done
SCRIPT
mcopy -i "$WORK/disk.img" "$WORK/W.TXT" ::/W.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /W.TXT" "WAIT:8" \
    "while cook taste.elf 1 = 1 ; do cook call.elf x > /dev/null ; done" "WAIT:4" "KEY:ctrl-c" "WAIT:3" \
    "cook call.elf after \$? > /W6.TXT" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; ( cd "$WORK/host" && sed -e 's|cook taste.elf|test|g' -e 's|cook call.elf|echo|g' -e 's| /W| W|g' "$WORK/W.TXT" > w.sh && sh w.sh ) >/dev/null 2>&1
got=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null | grep -oE "W[1-5]\.TXT" | sort | tr '\n' ' ')
want=$(cd "$WORK/host" && ls W[1-5].TXT | sort | tr '\n' ' ')
bad=0; [ "$got" = "$want" ] || { echo "        files: soupOS [$got] sh [$want]"; bad=1; }
for f in $want; do
    g=$(mcopy -n -i "$WORK/disk.img" "::/$f" - 2>/dev/null); w=$(cat "$WORK/host/$f")
    [ "$g" = "$w" ] || { echo "        $f: [$(echo "$g" | tr '\n' ' ')] sh [$(echo "$w" | tr '\n' ' ')]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    while loops leave what sh leaves: counting with \$((...)), a loop that never runs, for inside while, the status after" \
    || { echo "  FAIL  while differs from sh"; fail=1; }
[ "$(mcopy -n -i "$WORK/disk.img" ::/W6.TXT - 2>/dev/null)" = "after 130" ] \
    && echo "  ok    Ctrl-C stops an endless while; the shell carries on and \$? is 130" \
    || { echo "  FAIL  Ctrl-C and the loop: W6 [$(mcopy -n -i "$WORK/disk.img" ::/W6.TXT - 2>/dev/null)]"; fail=1; }
exit $fail

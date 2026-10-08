#!/usr/bin/env bash
# stat-test.sh - SYS_STAT: reaching a path needs search on the bowls above
# it, not read on the path (v0.59.0). Seen through taste.elf, which uses it.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-stat.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "stir ~/R.TXT secret recipe" "WAIT:1" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "cook taste.elf -f /home/saucier/R.TXT ; cook call.elf \$? > /S3.OUT" "WAIT:2" "clockout" "WAIT:1" \
    "cook" "soup" "WAIT:2" \
    "cook taste.elf -f /etc/kitchen ; cook call.elf \$? > ~/S1.OUT" "WAIT:2" \
    "cook taste.elf -e /home/saucier/R.TXT ; cook call.elf \$? > ~/S2.OUT" "WAIT:2" \
    "cook taste.elf -d /home/saucier ; cook call.elf \$? > ~/S4.OUT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null | tr -d '\n'; }
[ "$(val /home/cook/S1.OUT)" = "0" ] && echo "  ok    the cook's test -f sees /etc/kitchen, which they may not read (stat needs no read)" \
    || { echo "  FAIL  test -f /etc/kitchen as the cook: $(val /home/cook/S1.OUT)"; fail=1; }
[ "$(val /home/cook/S2.OUT)" = "1" ] && echo "  ok    but nothing inside saucier's closed home exists for them (no search on it)" \
    || { echo "  FAIL  the cook reached into a closed home: $(val /home/cook/S2.OUT)"; fail=1; }
[ "$(val /home/cook/S4.OUT)" = "0" ] && echo "  ok    the closed home itself is still a bowl to them (search on /home is enough)" \
    || { echo "  FAIL  test -d /home/saucier: $(val /home/cook/S4.OUT)"; fail=1; }
[ "$(val /S3.OUT)" = "0" ] && echo "  ok    the headchef reaches the file in the closed home" || { echo "  FAIL  the headchef's test -f: $(val /S3.OUT)"; fail=1; }
exit $fail

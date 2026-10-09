#!/usr/bin/env bash
# tilde-test.sh - ~ is the cook's home (v0.57.3).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tilde.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook call.elf ~ ~/X > /HC.TXT" "WAIT:2" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "cook call.elf hi > ~/H.TXT" "WAIT:2" "cook spoon.elf ~/H.TXT > ~/C.TXT" "WAIT:2" \
    "cd /" "WAIT:1" "cd ~" "WAIT:1" "pwd" "WAIT:1" "pour ~/H.TXT" "WAIT:1" \
    "cook call.elf ~ '~' \"~/x\" a~b ~x ~/ > ~/Q.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
[ "$(val /home/cook/H.TXT)" = "hi" ] && [ "$(val /home/cook/C.TXT)" = "hi" ] \
    && echo "  ok    > ~/H.TXT and spoon.elf ~/H.TXT land in the cook's home" || { echo "  FAIL  ~/ paths"; fail=1; }
L=$(tr -d '\r' < "$WORK/serial.log")
pwdline=$(echo "$L" | grep -A1 "> pwd" | tail -1)
[ "$pwdline" = "  /home/cook" ] && echo "$L" | grep -A1 "> pour ~/H.TXT" | tail -1 | grep -x "hi" >/dev/null \
    && echo "  ok    cd ~ goes home and pour ~/H.TXT reads there (builtins too)" || { echo "  FAIL  cd ~ or pour ~/ ($pwdline)"; fail=1; }
want=$(HOME=/home/cook sh -c "echo ~ '~' \"~/x\" a~b ~x ~/")
[ "$(val /home/cook/Q.TXT)" = "$want" ] && echo "  ok    as sh with HOME=/home/cook: [$want]" \
    || { echo "  FAIL  got [$(val /home/cook/Q.TXT)]  sh [$want]"; fail=1; }
[ "$(val /HC.TXT)" = "/ /X" ] && echo "  ok    the headchef's ~ is /, and ~/X is /X" || { echo "  FAIL  headchef ~: [$(val /HC.TXT)]"; fail=1; }
exit $fail

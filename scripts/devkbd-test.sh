#!/usr/bin/env bash
# devkbd-test.sh - a program reading /dev/kbd (v0.60.257, review R-K1). It
# used to read the keyboard with interrupts off inside the syscall, and the
# hlt waiting for a key never woke: the whole machine froze. It had no
# foreground check either, so a background program would have read what
# the console typed. Foreground: cook spoon.elf /dev/kbd gets a typed line,
# Ctrl-C ends it, and the shell answers after. Background: spoon reading
# /dev/kbd into /KEYS.TXT gets nothing, and a word typed at the shell after
# it is not in that file.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-devkbd.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook spoon.elf /dev/kbd" "WAIT:2" "TYPE:kbdline" "KEY:ret" "WAIT:1" "KEY:ctrl-c" "WAIT:2" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" \
    "cook spoon.elf /dev/kbd > /KEYS.TXT &" "WAIT:3" "parsnipword" "WAIT:1" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  the machine stopped answering: $(tr -d '\r' < "$WORK/log" | tail -2 | tr '\n' ' ')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
if [ "$fail" = 0 ]; then
    [ "$(grep -c 'A warm bowl' "$WORK/l")" -ge 2 ] && echo "  ok    the shell answers after both" || { echo "  FAIL  the shell did not answer twice"; fail=1; }
    grep -q '^kbdline' "$WORK/l" && echo "  ok    the foreground spoon read the typed line" || { echo "  FAIL  the foreground spoon never printed kbdline"; fail=1; }
    keys=$(mcopy -n -i "$WORK/disk.img" ::/KEYS.TXT - 2>/dev/null)
    case "$keys" in
        *parsnip*) echo "  FAIL  the background spoon read the console's keys: $keys"; fail=1 ;;
        *) echo "  ok    the background spoon read none of the console's keys" ;;
    esac
fi
[ "$fail" = 0 ] && echo "devkbd-test: PASS" || echo "devkbd-test: FAIL"
exit "$fail"

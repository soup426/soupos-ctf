#!/usr/bin/env bash
# writeshare-test.sh - one cook's writes cannot take everyone's budget
# (v0.60.292, review R-K30). v0.60.283's one budget for all let one cook's
# background writer (it outlives clockout) hold the machine's, and every
# other cook's redirect, soupyc write and neosim or Mincecraft save failed,
# the headchef's too. A cook may hold half of it now, all cooks together
# three quarters; the headchef has the rest. Here saucier's background
# /dev/zero writer is stopped at its share, saucier clocks out, and the
# headchef's small redirect reaches the disk.
# (v0.60.302, review R-T37) And the parts it could not see: sous's writer
# beside saucier's, the two stopped together near three quarters (at most
# 1.6 times saucier's half; without the cap they reach twice); then both
# killed and their files gone, saucier's share comes back: a redirect of
# saucier's reaches the disk.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-writeshare.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 200 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "hire sous" "WAIT:1" "thyme" "WAIT:1" "thyme" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "cook spoon.elf /dev/zero > /home/saucier/z &" "WAIT:10" "clockout" "WAIT:2" \
    "sous" "thyme" "WAIT:2" "cook spoon.elf /dev/zero > /home/sous/z &" "WAIT:10" "clockout" "WAIT:2" \
    "headchef" "rosemary" "WAIT:2" "cook spoon.elf /HELLO.TXT > /h.txt" "UNTIL:/spoon.elf exited with code" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
tr -d '\r' < "$WORK/log" > "$WORK/l"
pids=$(grep -o '\[proc [0-9]*\] spawned /spoon.elf /dev/zero' "$WORK/l" | grep -o 'proc [0-9]*' | grep -o '[0-9]*' | tr '\n' ' ')
set -- $pids
if [ $# -ge 2 ]; then
    QEMU_KEYS_NOWAIT=1 timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" "kill $1" "WAIT:2" "kill $2" "WAIT:8" \
        "strain /home/saucier/z" "WAIT:1" "strain /home/sous/z" "WAIT:1" "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" \
        "cook spoon.elf /HELLO.TXT > /home/saucier/h2.txt" "UNTIL:/spoon.elf exited with code" "WAIT:1" >/dev/null 2>&1 \
        || { echo "  FAIL  could not drive QEMU through the kills"; fail=1; }
else
    echo "  FAIL  the two writers' pids are not in the log: '$pids'"; fail=1
fi
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
hello=$(mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT - 2>/dev/null)
[ -n "$hello" ] || { echo "  FAIL  HELLO.TXT reads empty: the image is not readable"; fail=1; }
[ -n "$hello" ] && [ "$(mcopy -n -i "$WORK/disk.img" ::/h.txt - 2>/dev/null)" = "$hello" ] \
    && echo "  ok    with the cooks' writers still running, the headchef's redirect reached the disk" \
    || { echo "  FAIL  the headchef's redirect: $(grep -o '/h.txt: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
s1=$(grep -o "/home/saucier/z: stops at [0-9]* bytes, the cook's share of the write budget" "$WORK/l" | head -1 | grep -o '[0-9]*' | head -1)
s2=$(grep -o "/home/sous/z: stops at [0-9]* bytes, the cook's share of the write budget" "$WORK/l" | head -1 | grep -o '[0-9]*' | head -1)
[ -n "$s1" ] && echo "  ok    saucier's writer was stopped at its share: $s1 bytes" \
    || { echo "  FAIL  saucier's writer: $(grep -o '/home/saucier/z: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
if [ -n "$s1" ] && [ -n "$s2" ] && [ $(( (s1 + s2) * 10 )) -le $(( s1 * 16 )) ]; then
    echo "  ok    sous's writer beside it was stopped at $s2: the two hold $((s1 + s2)) bytes, near three quarters (saucier's half is $s1)"
else
    echo "  FAIL  the cooks together: saucier ${s1:-?}, sous ${s2:-?} ($(grep -o '/home/sous/z: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)); want at most 1.6 times saucier's"; fail=1
fi
[ -n "$hello" ] && [ "$(mcopy -n -i "$WORK/disk.img" ::/home/saucier/h2.txt - 2>/dev/null)" = "$hello" ] \
    && echo "  ok    the writers killed and their files gone, saucier's share came back: saucier's redirect reached the disk" \
    || { echo "  FAIL  saucier's redirect after the kills: $(grep -o '/home/saucier/h2.txt: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
[ "$fail" = 0 ] && echo "writeshare-test: PASS" || echo "writeshare-test: FAIL"
exit "$fail"

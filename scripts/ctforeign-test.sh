#!/usr/bin/env bash
# ctforeign-test.sh - another cook's program gets no window on this desktop
# (v0.60.259, review R-C1). A window goes on top and takes the focus, so it
# had the desktop's keys: any cook's program, from anywhere, could open one
# on another cook's desktop and log their typing (and write on their bar).
# Here saucier starts prod.elf keys in the background (it waits for a
# desktop) and clocks out; the headchef clocks in, starts countertop and
# presses s and e. saucier's window is refused, once in the log, and no
# key reaches it. The headchef's own prod.elf keys still gets its window.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-ctforeign.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "cook prod.elf keys &" "WAIT:1" "clockout" "WAIT:2" \
    "headchef" "rosemary" "WAIT:1" "cook prod.elf keys &" "WAIT:1" "countertop" "WAIT:5" \
    "KEY:s" "WAIT:1" "KEY:e" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
spid=$(grep -m1 -oE '^\[proc [0-9]+\] spawned /prod\.elf' "$WORK/l" | grep -oE '[0-9]+')
hpid=$(grep -oE '^\[proc [0-9]+\] spawned /prod\.elf' "$WORK/l" | sed -n 2p | grep -oE '[0-9]+')
[ -n "$spid" ] && [ -n "$hpid" ] || { echo "  FAIL  two prod.elf never started: $(grep 'prod' "$WORK/l" | head -3 | tr '\n' ';')"; fail=1; }
if [ "$fail" = 0 ]; then
    grep -q "^\[countertop\] pid $spid asks for" "$WORK/l" && { echo "  FAIL  saucier's prod (pid $spid) got a window on the headchef's desktop"; fail=1; }
    n=$(grep -c "^\[countertop\] pid $spid (saucier) refused: not this desktop's cook" "$WORK/l")
    [ "$n" = 1 ] && echo "  ok    saucier's program refused a window on the headchef's desktop, logged once" \
        || { echo "  FAIL  the refusal logged $n times"; fail=1; }
    grep -q "^\[countertop\] pid $hpid asks for a 160x80 window" "$WORK/l" && echo "  ok    the headchef's own prod.elf keys still got its window" \
        || { echo "  FAIL  the headchef's own prod got no window"; fail=1; }
fi
[ "$fail" = 0 ] && echo "ctforeign-test: PASS" || echo "ctforeign-test: FAIL"
exit "$fail"

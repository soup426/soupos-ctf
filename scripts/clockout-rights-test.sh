#!/usr/bin/env bash
# clockout-rights-test.sh - a program's rights are its own cook's (v0.60.258,
# review R-K2). A program's open, stat, kill and the rest were decided by
# its shell's CURRENT cook, so one left running after clockout had the
# next cook's rights: saucier's background program read /etc/kitchen once
# the headchef clocked in. Here saucier starts two infuse.elf in the
# background, each waiting for /GO: one opens /etc/kitchen, the other a
# file of saucier's own; saucier clocks out, the headchef clocks in (slowly:
# a pause before the secret) and only then makes /GO (v0.60.260, review
# R-T19: a fixed wait could wake them at the login screen, refused there
# whatever the kernel did). The first is refused, the second still reads (its cook's
# rights go with it), and no secret line is ever printed.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-clockrights.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "stir mine.txt sauciers own" "WAIT:1" \
    "cook infuse.elf /GO /etc/kitchen &" "WAIT:1" "cook infuse.elf /GO /home/saucier/mine.txt &" "WAIT:1" \
    "clockout" "WAIT:2" "headchef" "WAIT:4" "rosemary" "WAIT:2" "stir /GO go" \
    "UNTIL:INFUSE" "WAIT:3" "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | tail -2 | tr '\n' ' ')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep -q 'INFUSE read: headchef:' "$WORK/l" && { echo "  FAIL  saucier's program read /etc/kitchen as the headchef: $(grep -m1 'INFUSE read: headchef' "$WORK/l" | cut -c1-40)..."; fail=1; }
w=$(grep -n 'Welcome to the kitchen, headchef' "$WORK/l" | tail -1 | cut -d: -f1)
r=$(grep -n 'INFUSE refused' "$WORK/l" | head -1 | cut -d: -f1)
[ -n "$w" ] && [ -n "$r" ] && [ "$r" -lt "$w" ] && { echo "  FAIL  the refusal came before the headchef clocked in (line $r, Welcome $w): it proves nothing"; fail=1; }
grep -q 'INFUSE refused' "$WORK/l" && echo "  ok    saucier's program left running was refused /etc/kitchen" \
    || { echo "  FAIL  no refusal for /etc/kitchen: $(grep INFUSE "$WORK/l" | tr '\n' ';')"; fail=1; }
grep -q "INFUSE read: sauciers own" "$WORK/l" && echo "  ok    and still read saucier's own file: its cook's rights went with it" \
    || { echo "  FAIL  saucier's own file was not read: $(grep INFUSE "$WORK/l" | tr '\n' ';')"; fail=1; }
[ "$fail" = 0 ] && echo "clockout-rights-test: PASS" || echo "clockout-rights-test: FAIL"
exit "$fail"

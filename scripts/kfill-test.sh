#!/usr/bin/env bash
# kfill-test.sh - a syscall's copy into heap the pager cannot give is a -1,
# not a panic (v0.60.269, review R-K3). user_ok passed any page below the
# break as one the demand pager would give, but at the page cap with the
# swap full it gives none: the kernel's own copy faulted in ring 0, the
# pager declined, and isr.c panicked. proof.elf kfill claims 32 MB (past the
# cap and the swap together) and has the kernel write every page through
# sys_random. A call must be refused (LAZY kernel refused page N), with no
# panic, and the shell must answer after. Then proof.elf kbig N: one read
# into N fresh pages (v0.60.273), no panic either way.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-kfill.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook proof.elf kfill" "UNTIL:/proof.elf exited with code" "WAIT:1" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1
# (v0.60.273, review R-K15) Then ONE read into a fresh buffer of exactly as
# many pages as kfill could have: v0.60.269's pre-fault evicted its first
# pages to the last slot and the copy into page 0 panicked.
N=$(tr -d '\r' < "$WORK/log" | grep -o 'LAZY kernel refused page [0-9]*' | head -1 | grep -o '[0-9]*$')
[ -n "$N" ] && QEMU_KEYS_NOWAIT=1 timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" \
    "cook proof.elf kbig $N" "UNTIL:/proof.elf exited with code" "WAIT:1" \
    "pour README.TXT" "UNTIL:A 32-bit hobby OS" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
if grep -q 'KERNEL PANIC' "$WORK/l"; then
    echo "  FAIL  the kernel panicked: $(grep -m1 'fault addr' "$WORK/l")"; fail=1
else
    echo "  ok    no panic"
fi
grep -q 'LAZY kernel refused page [0-9]* of 8192' "$WORK/l" && echo "  ok    $(grep -o 'LAZY kernel refused page [0-9]* of 8192' "$WORK/l" | head -1): a -1, past the cap and the swap" \
    || { echo "  FAIL  no refusal: $(grep -o 'LAZY.*' "$WORK/l" | head -1)"; fail=1; }
grep -q 'A warm bowl' "$WORK/l" && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
grep -q "LAZY one read into ${N:-?} pages gave -\?[0-9]" "$WORK/l" && echo "  ok    $(grep -o 'LAZY one read into [0-9]* pages gave -\?[0-9]*' "$WORK/l" | head -1): no panic" \
    || { echo "  FAIL  the one big read: $(grep -o 'LAZY one.*' "$WORK/l" | head -1)"; fail=1; }
grep -q 'A 32-bit hobby OS' "$WORK/l" && echo "  ok    the shell answers after it" || { echo "  FAIL  the shell never answered after the big read"; fail=1; }
[ "$fail" = 0 ] && echo "kfill-test: PASS" || echo "kfill-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# appendshare-test.sh - a cook can append to their own big file (v0.60.306,
# review R-K35). An append starts its buffer with what the file holds, and
# v0.60.292's per-cook share counted those pages too: a cook could not >>
# onto their own file once it was bigger than half the disk's free room (a
# plain "cannot open", nothing logged). The old contents are counted in the
# shared cap now (the close rewrites the whole file) but not in the share.
# Here saucier writes a 12 MB file with a script (under half the room then),
# so afterwards the room is about 16 MB and saucier's share 8; a >> of one
# line onto it must reach the disk.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-appendshare.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/mk.sc" <<'SC'
let f = open("/home/saucier/big.log", "w")
let s = "xxxxxxxx"
for k in 1..10 {
    s = s + s
}
for k in 1..3073 {
    write(f, s)
}
close(f)
pour "MA" + "DE"
SC
mcopy -i "$WORK/disk.img" "$WORK/mk.sc" ::/MK.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 200 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "soup /MK.SC" "UNTIL:MADE" "WAIT:2" \
    "cook call.elf one more line >> /home/saucier/big.log" "WAIT:2" "pour /HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep -E 'big.log|MADE|cannot' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/home/saucier/big.log "$WORK/big" 2>/dev/null
sz=$(wc -c < "$WORK/big" 2>/dev/null); tl=$(tail -c 20 "$WORK/big" 2>/dev/null | tr -d '\n')
[ "${sz:-0}" -gt 12578816 ] && [ "${tl%one more line}" != "$tl" ] && echo "  ok    saucier's >> onto their 12 MB file reached the disk: $sz bytes, ending '$tl'" \
    || { echo "  FAIL  /home/saucier/big.log is ${sz:-no} bytes, ending '${tl:-}': $(tr -d '\r' < "$WORK/log" | grep -E 'big.log' | tail -2 | tr '\n' ';')"; fail=1; }
[ "$fail" = 0 ] && echo "appendshare-test: PASS" || echo "appendshare-test: FAIL"
exit "$fail"

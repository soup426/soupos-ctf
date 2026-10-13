#!/usr/bin/env bash
# killclose-test.sh - killing a spawned script is safe wherever it is
# (v0.60.300, reviews R-K33, R-K34). v0.60.290 had kill mark the task dead
# where it stood and release its files from the killer: killed inside
# close(), the dead task still held fat_mtx for the flush and the killer's
# own close of the same node waited on it for good (the shell hung), and a
# cook's file was flushed from the headchef's shell, so it was created as
# the headchef's. Now kill asks, and the script stops at its next statement
# (or sleep, or write) and closes its own files. Here saucier spawns a
# script holding /home/saucier/own.txt open and clocks out; the headchef
# kills it: the file is saucier's. Then the headchef spawns one that writes
# 12 MB (3071 writes of 4096: soupyc's ranges leave out their end), says
# CLOSING and closes; killed just after, the shell answers and the file
# holds all 12578816 bytes.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-killclose.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/own.sc" <<'SC'
fn w() {
    let f = open("/home/saucier/own.txt", "w")
    write(f, "saucier's")
    while 1 {
        sleep(500)
    }
}
pour "OT" + "ID " + spawn("w")
SC
cat > "$WORK/big.sc" <<'SC'
fn w() {
    sleep(4000)
    let f = open("/big.txt", "w")
    let s = "xxxxxxxx"
    for k in 1..10 {
        s = s + s
    }
    for k in 1..3072 {
        write(f, s)
    }
    pour "CLO" + "SING"
    close(f)
    while 1 {
        sleep(500)
    }
}
pour "BT" + "ID " + spawn("w")
SC
mcopy -i "$WORK/disk.img" "$WORK/own.sc" ::/OWN.SC; mcopy -i "$WORK/disk.img" "$WORK/big.sc" ::/BIG.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "soup /OWN.SC" "UNTIL:OTID" "WAIT:2" "clockout" "WAIT:2" \
    "headchef" "rosemary" "WAIT:2" "soup /BIG.SC" "UNTIL:BTID" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU to the two scripts"; fail=1; }
tr -d '\r' < "$WORK/log" > "$WORK/l"
OTID=$(grep -o 'OTID [0-9]*' "$WORK/l" | head -1 | grep -o '[0-9]*$'); BTID=$(grep -o 'BTID [0-9]*' "$WORK/l" | head -1 | grep -o '[0-9]*$')
if [ -n "$OTID" ] && [ -n "$BTID" ]; then
    QEMU_KEYS_NOWAIT=1 timeout 60 python3 scripts/qemu_keys.py "$WORK/mon.sock" "kill $OTID" "UNTIL:killed task $OTID" "WAIT:1" \
        "UNTIL:CLOSING" "kill $BTID" "UNTIL:killed task $BTID" "WAIT:1" "perms /home/saucier/own.txt" "UNTIL:owner:" "WAIT:1" \
        "pour HELLO.TXT" "UNTIL:A warm bowl" "WAIT:1" >/dev/null 2>&1 \
        || { echo "  FAIL  the shell stopped answering after the kills: $(tr -d '\r' < "$WORK/log" | grep -E 'killed task|CLOSING|kill' | tail -3 | tr '\n' ';')"; fail=1; }
else
    echo "  FAIL  a script never said its task id (OTID=${OTID:-} BTID=${BTID:-})"; fail=1
fi
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    killed just after CLOSING, the shell still answers" || { echo "  FAIL  the shell never answered after the kill"; fail=1; }
sz=$(mcopy -n -i "$WORK/disk.img" ::/big.txt - 2>/dev/null | wc -c)
[ "$sz" = 12578816 ] && echo "  ok    the killed script's file holds all it wrote: $sz bytes" || { echo "  FAIL  /big.txt is ${sz:-no} bytes (want 12578816)"; fail=1; }
own=$(grep -o 'owner: [a-z]*' "$WORK/l" | tail -1)
[ "$own" = "owner: saucier" ] && echo "  ok    saucier's file, flushed when the headchef killed the script, is saucier's" || { echo "  FAIL  /home/saucier/own.txt: '${own:-no perms line}' (want owner: saucier)"; fail=1; }
[ "$(mcopy -n -i "$WORK/disk.img" ::/home/saucier/own.txt - 2>/dev/null)" = "saucier's" ] && echo "  ok    and holds what the script wrote" || { echo "  FAIL  /home/saucier/own.txt does not hold what was written"; fail=1; }
grep 'KERNEL PANIC' "$WORK/l" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
[ "$fail" = 0 ] && echo "killclose-test: PASS" || echo "killclose-test: FAIL"
exit "$fail"

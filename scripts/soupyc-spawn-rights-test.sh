#!/usr/bin/env bash
# soupyc-spawn-rights-test.sh - a spawn()ed soupyc task keeps its cook's
# rights (v0.60.266, review R-K14). It is a kernel task with no program, so
# the uid resolver gave it its shell's CURRENT cook: one left running after
# clockout opened files as the next cook (and, since v0.60.262 made soupyc
# ask users_may at all, that is a read of anything the next cook may read).
# saucier spawns a worker that waits for /home/saucier/go.txt, then opens
# /etc/kitchen; saucier clocks out, the headchef clocks in and makes
# go.txt. The worker must be refused, and still read a file of saucier's.
# The script is a file on the disk (/SPAWNW.SC): one line of it is past the
# shell's line.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-scspawn.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/w.sc" <<'SC'
fn w() {
    let g = -1
    while g < 0 {
        sleep(200)
        g = open("/home/saucier/go.txt")
    }
    let f = open("/etc/kitchen")
    let m = open("/home/saucier/mine.txt")
    pour "SPAWNED " + f + " " + m
}
spawn("w")
pour "WORKER" + " UP"
SC
mcopy -i "$WORK/disk.img" "$WORK/w.sc" ::/SPAWNW.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "stir mine.txt sauciers own" "WAIT:1" \
    "soup /SPAWNW.SC" "UNTIL:WORKER UP" "WAIT:1" "clockout" "WAIT:2" "headchef" "rosemary" "WAIT:2" \
    "stir /home/saucier/go.txt go" "UNTIL:SPAWNED" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | tail -2 | tr '\n' ' ')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
got=$(grep -oE 'SPAWNED -?[0-9]+ -?[0-9]+' "$WORK/l" | head -1)
case "$got" in
    "SPAWNED -1 "[0-9]*) echo "  ok    saucier's spawned worker was refused /etc/kitchen after the headchef clocked in, and still opened saucier's own file" ;;
    "SPAWNED -1 -1") echo "  FAIL  the worker could open neither: $got"; fail=1 ;;
    "") echo "  FAIL  the worker never reported"; fail=1 ;;
    *) echo "  FAIL  saucier's worker opened /etc/kitchen as the headchef: $got"; fail=1 ;;
esac
[ "$fail" = 0 ] && echo "soupyc-spawn-rights-test: PASS" || echo "soupyc-spawn-rights-test: FAIL"
exit "$fail"

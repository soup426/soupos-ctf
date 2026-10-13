#!/usr/bin/env bash
# writebudget-test.sh - all the write buffers together hold no more than the
# disk's room (v0.60.283, review R-K24). v0.60.275 capped each write handle at the
# disk's room, but per handle: five at once each took 28 MB, memory ran out
# under them, and everything else that needed a frame starved. One count of
# the pages every write buffer holds is kept now, capped at the disk's
# room (28 MB on the stock disk, so at -m 128M memory is never all theirs).
# Here five background spoons write /dev/zero into five files at once for
# 20 s: no write runs out of memory, the shared cap is what stops them, and
# the shell answers after.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-writebudget.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/leak.sc" <<'SC'
fn w() {
    let f = open("/leak.txt", "w")
    let s = "xxxxxxxx"
    for k in 1..10 {
        s = s + s
    }
    while write(f, s) > 0 {
        let n = 0
    }
    while 1 {
        sleep(500)
    }
}
pour "TI" + "D " + spawn("w")
SC
mcopy -i "$WORK/disk.img" "$WORK/leak.sc" ::/LEAK.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys=("headchef" "rosemary" "WAIT:1")
for i in 1 2 3 4 5; do keys+=("cook spoon.elf /dev/zero > /z$i &" "WAIT:1"); done
keys+=("WAIT:20")
for i in 1 2 3 4 5; do keys+=("kill %$i" "WAIT:1"); done
# (v0.60.290, reviews R-T32, R-K29) The writers gone, their pages must come
# off the count: a small redirect reaches the disk. Then a spawned soupyc
# writer fills the budget and is killed by its task id: its files must be
# closed and its pages given back too, and a second redirect still works.
# A killed writer's file is flushed at its close (the first one's 28 MB fills
# the disk), so the files go before each small redirect: what is measured is
# the count coming back, not a full disk.
keys+=("WAIT:3")
for i in 1 2 3 4 5; do keys+=("strain /z$i" "WAIT:1"); done
keys+=("cook spoon.elf /HELLO.TXT > /after1.txt" "UNTIL:/spoon.elf exited with code" "WAIT:1"
       "soup /LEAK.SC" "UNTIL:/leak.txt: stops at" "WAIT:2")
timeout 150 python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1
TID=$(tr -d '\r' < "$WORK/log" | grep -o 'TID [0-9]*' | head -1 | grep -o '[0-9]*$')
[ -n "$TID" ] && QEMU_KEYS_NOWAIT=1 timeout 90 python3 scripts/qemu_keys.py "$WORK/mon.sock" "kill $TID" "WAIT:3" "strain /leak.txt" "WAIT:2" \
    "cook spoon.elf /HELLO.TXT > /after2.txt" "UNTIL:/spoon.elf exited with code" "WAIT:1" "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
hello=$(mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT - 2>/dev/null)
[ "$(mcopy -n -i "$WORK/disk.img" ::/after1.txt - 2>/dev/null)" = "$hello" ] && echo "  ok    after the writers were killed their pages came back: a small redirect reached the disk" \
    || { echo "  FAIL  a redirect after the writers were killed: $(grep -o '/after1.txt: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
[ -n "$TID" ] || { echo "  FAIL  the soupyc writer never said its task id"; fail=1; }
[ "$(mcopy -n -i "$WORK/disk.img" ::/after2.txt - 2>/dev/null)" = "$hello" ] && echo "  ok    after the soupyc writer (task ${TID:-?}) was killed its pages came back too" \
    || { echo "  FAIL  a redirect after the soupyc writer was killed: $(grep -o '/after2.txt: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | head -1)"; fail=1; }
grep 'out of memory at' "$WORK/l" >/dev/null && { echo "  FAIL  a write ran out of memory: $(grep -o '/z[0-9]: out of memory at [0-9]* bytes' "$WORK/l" | head -1)"; fail=1; }
grep 'write buffers together hold the disk.s room' "$WORK/l" >/dev/null && echo "  ok    the shared cap stopped them: $(grep -o '/z[0-9]: stops at [0-9]* bytes, the write buffers together hold the disk.s room' "$WORK/l" | head -1)" \
    || { echo "  FAIL  the shared cap never stopped a write: $(grep -o '/z[0-9]: [a-z ]* at [0-9]* bytes[^;]*' "$WORK/l" | tr '\n' ';')"; fail=1; }
grep 'KERNEL PANIC' "$WORK/l" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered after"; fail=1; }
[ "$fail" = 0 ] && echo "writebudget-test: PASS" || echo "writebudget-test: FAIL"
exit "$fail"

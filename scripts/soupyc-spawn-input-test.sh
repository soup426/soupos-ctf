#!/usr/bin/env bash
# soupyc-spawn-input-test.sh - a spawn()ed script's input gets no keys
# (v0.60.274, review R-K17). input reads its terminal's getc, which on the
# console is the keyboard itself, with no foreground rule: a spawned script
# raced the shell (and, outliving clockout, the next login) for the keys,
# the hole v0.60.264 closed for /dev/kbd by another door. A worker spawned
# from /SPAWNI.SC reads input and prints it; then a word is typed at the
# shell. The worker gets an empty line at once, and the shell the whole word.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-scinput.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/i.sc" <<'SC'
fn w() {
    let s = input
    pour "GOT [" + s + "]"
}
spawn("w")
pour "WORKER" + " UP"
SC
mcopy -i "$WORK/disk.img" "$WORK/i.sc" ::/SPAWNI.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 90 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "soup /SPAWNI.SC" "UNTIL:WORKER UP" "WAIT:2" "parsnipword" "WAIT:2" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" >/dev/null 2>&1
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep -F 'GOT []' "$WORK/l" >/dev/null && echo "  ok    the spawned worker's input was an empty line" \
    || { echo "  FAIL  the worker's input: $(grep -o 'GOT \[.*' "$WORK/l" | head -1 || true)"; fail=1; }
grep 'GOT \[.\+\]' "$WORK/l" >/dev/null && { echo "  FAIL  the worker took keys: $(grep -o 'GOT \[.*' "$WORK/l" | head -1)"; fail=1; }
grep "'parsnipword'" "$WORK/l" >/dev/null && echo "  ok    the shell had the whole word" \
    || { echo "  FAIL  the shell lost part of the word: $(grep -o "'[a-z]*nipword'\|[a-z]*nipword" "$WORK/l" | head -1)"; fail=1; }
grep 'A warm bowl' "$WORK/l" >/dev/null && echo "  ok    the shell answers after" || { echo "  FAIL  the shell never answered"; fail=1; }
[ "$fail" = 0 ] && echo "soupyc-spawn-input-test: PASS" || echo "soupyc-spawn-input-test: FAIL"
exit "$fail"

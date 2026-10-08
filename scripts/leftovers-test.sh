#!/usr/bin/env bash
# leftovers-test.sh - a cook's history outlives the shift (v0.60.17).
#
# First boot: the headchef and the saucier each type lines and clock out,
# then the headchef types one more and reheats (QEMU runs with -no-reboot,
# so the reboot ends it). Second boot of the same disk:
# the up arrow brings back the headchef's last line, `leftovers` lists each
# cook's own lines and nobody else's, and the saucier cannot read the
# headchef's file, from the shell or from a ring-3 program.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-leftovers.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
start() {
    rm -f "$WORK/mon.sock"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -display none \
        -serial "file:$1" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
}
keys() { python3 scripts/qemu_keys.py "$WORK/mon.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }; }
stop() { kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""; }
P="UNTIL:@soupOS:"

start "$WORK/one.log"
keys "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" \
     "slurp first-one" "$P" "clockout" "WAIT:1" \
     "saucier" "basil" "UNTIL:Welcome to the kitchen, saucier" "slurp saucier-line" "$P" "clockout" "WAIT:1" \
     "headchef" "rosemary" "UNTIL:Welcome to the kitchen, headchef" "slurp last-of-boot-one" "$P" "reheat"
for _ in $(seq 1 100); do kill -0 "$PID" 2>/dev/null || break; sleep 0.1; done
kill -0 "$PID" 2>/dev/null && { echo "  FAIL  reheat did not end the first boot"; fail=1; }
stop

start "$WORK/two.log"
keys "headchef" "rosemary" "WAIT:1" "KEY:up" "KEY:up" "" "$P" "leftovers" "$P" "clockout" "WAIT:1" \
     "saucier" "basil" "UNTIL:Welcome to the kitchen, saucier" "leftovers" "$P" \
     "pour /etc/LEFTOVER" "$P" "cook spoon.elf /etc/LEFTOVER" "$P" "WAIT:1"
stop

tr -d '\r' < "$WORK/two.log" > "$WORK/two.txt"
sed -n '1,/Welcome to the kitchen, saucier/p' "$WORK/two.txt" > "$WORK/head.txt"
sed -n '/Welcome to the kitchen, saucier/,$p' "$WORK/two.txt" > "$WORK/sauc.txt"
has() { grep -Exq -- "$2" "$WORK/$1"; }
has head.txt 'last-of-boot-one' && echo "  ok    after a reheat, the up arrow brings back the headchef's line before it" \
    || { echo "  FAIL  the up arrow after a reheat did not rerun 'slurp last-of-boot-one'"; fail=1; }
has head.txt ' +[0-9]+  slurp first-one' && has head.txt ' +[0-9]+  hire saucier' \
    && echo "  ok    the headchef's leftovers survive a clockout and a reboot" \
    || { echo "  FAIL  the headchef's leftovers after a reboot are missing lines"; fail=1; }
has sauc.txt ' +[0-9]+  slurp saucier-line' && echo "  ok    the saucier's leftovers are the saucier's own" \
    || { echo "  FAIL  the saucier's leftovers after a reboot are missing 'slurp saucier-line'"; fail=1; }
if grep -Eq 'first-one|hire saucier|last-of-boot-one' "$WORK/sauc.txt"; then
    echo "  FAIL  the saucier saw the headchef's history:"; grep -En 'first-one|hire saucier|last-of-boot-one' "$WORK/sauc.txt" | sed 's/^/        /'; fail=1
else echo "  ok    the saucier never sees the headchef's lines (leftovers, pour, a ring-3 read)"; fi
grep -q 'spoon.elf' "$WORK/sauc.txt" || { echo "  FAIL  the ring-3 read never ran"; fail=1; }
if mtype -i "$WORK/disk.img" ::/home/saucier/LEFTOVER 2>/dev/null | grep -x 'slurp saucier-line' >/dev/null; then
    echo "  ok    the saucier's file is /home/saucier/LEFTOVER on the disk"
else echo "  FAIL  /home/saucier/LEFTOVER does not hold the saucier's line"; fail=1; fi
exit $fail

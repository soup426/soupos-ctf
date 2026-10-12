#!/usr/bin/env bash
# mincecraft-craft-test.sh - crafting (v0.60.215): c on the hotbar's block.
# The flat world has a broccoli stalk two high just left of the spawn
# (63, 11-12, 64). Turned left to it, F breaks its top into the hand; c on it
# makes four toast. Looking down, F digs the crust and two parmesan; c on
# the parmesan makes a cheese grater from two; c on toast says nothing can be
# made from it. The inventory in the markers, before and after each.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mccraft.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -time 600 -world /C.WLD &" "WAIT:1" "countertop" "WAIT:5"
      "KEY:left 1600" "WAIT:1" "KEY:f 1500" "WAIT:2" "TYPE:1" "TYPE:c" "WAIT:1"
      "KEY:down 2000" "WAIT:3" "KEY:f 2600" "WAIT:5" "TYPE:2" "TYPE:c" "WAIT:2" "TYPE:2" "TYPE:c" "WAIT:2"
      "KEY:esc" "WAIT:4")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], 'rb').read().decode('latin-1')
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
check(re.search(r'\[mincecraft\] broke 63 12 64 7\b', log), 'the stalk\'s top broken into the hand (63 12 64, broccoli stalk)')
check('[mincecraft] crafted 4 toast from 1 broccoli stalk' in log, 'c on the stalk: four toast')
check(re.search(r'\[mincecraft\] inv(?=[^\n]*\b7=0\b)(?=[^\n]*\b11=4\b)', log), 'the hand: no stalk, four toast (inv 7=0 11=4)')
check(len(re.findall(r'\[mincecraft\] broke 64 \d+ 64 3\b', log)) >= 2, 'two parmesan dug from underfoot')
check('[mincecraft] crafted 1 cheese grater from 2 parmesan' in log, 'c on the parmesan: a cheese grater')
check(re.search(r'\[mincecraft\] inv(?=[^\n]*\b3=0\b)(?=[^\n]*\b12=1\b)', log), 'the hand: no parmesan, a grater (inv 3=0 12=1)')
check('[mincecraft] craft: nothing to make from toast' in log, 'c on toast: nothing to make from it')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-craft-test: PASS" || echo "mincecraft-craft-test: FAIL"
exit "$fail"

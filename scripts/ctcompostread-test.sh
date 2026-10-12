#!/usr/bin/env bash
# ctcompostread-test.sh - a file the cook may write but not read is not
# composted (v0.60.263, review R-C2). Removing a file in the files window
# copies it into the cook's own ~/compost, and only 'w' was asked: the
# headchef's /drop/memo.txt, rw--w- in an open bowl, became saucier's to
# read. Here saucier removes it (asked twice): it is refused, stays where it
# was, and nothing of it reaches saucier's compost.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcompostread.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mapfile -d '' -t keys < <(python3 - "$WORK" <<'PY'
import sys
work = sys.argv[1]
out = ['headchef', 'rosemary', 'WAIT:1', 'mkbowl /drop', 'WAIT:1', 'perms /drop rwxrwx', 'WAIT:1',
       'stir /drop/memo.txt topsecretpayroll', 'WAIT:1', 'perms /drop/memo.txt rw--w-', 'WAIT:1',
       'hire saucier', 'WAIT:1', 'basil', 'WAIT:1', 'basil', 'WAIT:1', 'clockout', 'WAIT:2',
       'saucier', 'basil', 'WAIT:2', 'pour /drop/memo.txt', 'WAIT:1',
       'countertop', 'WAIT:2'] + ['MON:mouse_move -150 -150'] * 8
pos = [0, 0]
def go(x, y):
    while pos != [x, y]:
        dx = max(-150, min(150, x - pos[0])); dy = max(-150, min(150, y - pos[1]))
        out.append(f'MON:mouse_move {dx} {dy}'); pos[0] += dx; pos[1] += dy
    out.append('WAIT:1')
click = ['MON:mouse_button 1', 'WAIT:1', 'MON:mouse_button 0', 'WAIT:2']
right = ['MON:mouse_button 4', 'WAIT:1', 'MON:mouse_button 0', 'WAIT:2']
twice = ['MON:mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0', 'WAIT:3']
row = lambda i: 120 + 18 * i                       # the files window at 604,60: row i's middle
def menu(r, item, times=1):                        # right click on row r, then the menu's entry `item`
    go(700, row(r)); out.extend(right); go(710, row(r) + 2 + 22 * item + 11)
    for _ in range(times): out.extend(click)
go(350, 12); out += click                          # Files at /
go(700, row(1)); out += twice                      # drop/, row 1 in directory order (etc/, drop/, home/): .., memo.txt
menu(1, 1, 2)                                      # remove memo.txt (asked twice)
go(1000, 700); out += ['WAIT:2']
print('\0'.join(out), end='')
PY
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -q '^\[countertop\] files in /drop' "$WORK/log" || { echo "        the files window never reached /drop"; fail=1; }
grep -q '^\[countertop\] may not compost /drop/memo.txt: not readable' "$WORK/log" \
    || { echo "        no refusal: $(grep -E '^\[countertop\] (composted|may not|removed)' "$WORK/log" | tr '\n' ';')"; fail=1; }
mcopy -n -i "$WORK/disk.img" ::/drop/memo.txt - 2>/dev/null | grep topsecretpayroll >/dev/null || { echo "        /drop/memo.txt is gone from /drop"; fail=1; }
mdir -i "$WORK/disk.img" ::/home/saucier/compost/memo.txt >/dev/null 2>&1 && { echo "        memo.txt reached saucier's compost"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    a write-only file is not composted: refused, left where it was, nothing in saucier's compost" \
    || echo "  FAIL  composting a file the cook may not read"
exit $fail

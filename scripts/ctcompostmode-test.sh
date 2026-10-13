#!/usr/bin/env bash
# ctcompostmode-test.sh - a composted file keeps its mode, and the compost
# is its cook's alone (v0.60.270, review R-C16). Compost, copy and put back
# made every new file rwxr-x, whatever the old one's mode, and the
# headchef's compost is /compost at the root, also rwxr-x: a rw---- file the
# headchef composted was every cook's to read, and /compost/.whence named
# where each came from. Here the headchef composts /drop/priv.txt (rw----)
# in the files window, leaves and clocks out; saucier is then refused both
# /compost/priv.txt and /compost/.whence, and its mode still reads rw----.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcompostmode.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mapfile -d '' -t keys < <(python3 - "$WORK" <<'PY'
import sys
out = ['headchef', 'rosemary', 'WAIT:1', 'mkbowl /drop', 'WAIT:1',     # before hire: drop/ takes row 1, as in ctcompostread-test
       'stir /drop/priv.txt headchefsecretpayroll', 'WAIT:1', 'perms /drop/priv.txt rw----', 'WAIT:1',
       'hire saucier', 'WAIT:1', 'basil', 'WAIT:1', 'basil', 'WAIT:1',
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
go(700, row(1)); out += twice                      # drop/, row 1 in directory order (etc/, drop/, home/): .., priv.txt
menu(1, 1, 2)                                      # remove priv.txt (asked twice): to /compost
go(288, 12); out += click + ['WAIT:2']             # Leave
out += ['clockout', 'WAIT:2', 'saucier', 'basil', 'WAIT:2',
        'pour /compost/priv.txt', 'WAIT:1', 'pour /compost/.whence', 'WAIT:1',
        'perms /compost/priv.txt', 'WAIT:1', 'slurp MODE-DONE', 'UNTIL:MODE-DONE']
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
grep '^\[countertop\] composted /drop/priv.txt as /compost/priv.txt' "$WORK/log" >/dev/null \
    || { echo "        not composted: $(grep -E '^\[countertop\] (composted|may not|files in)' "$WORK/log" | tr '\n' ';')"; fail=1; }
grep headchefsecretpayroll "$WORK/log" | grep -v 'stir /drop' >/dev/null && { echo "        saucier read /compost/priv.txt"; fail=1; }
grep 'priv.txt	/drop' "$WORK/log" >/dev/null && { echo "        saucier read /compost/.whence"; fail=1; }
[ "$(grep -c 'Permission denied: /compost/' "$WORK/log")" -ge 2 ] || { echo "        not both refused: $(grep -c 'Permission denied: /compost/' "$WORK/log") refusals"; fail=1; }
[ "$(grep -o 'perms: [rwx-]*' "$WORK/log" | tail -1)" = "perms: rw----" ] || { echo "        the composted file's mode: $(grep -o 'perms: [rwx-]*' "$WORK/log" | tail -1)"; fail=1; }   # the last perms: saucier's
[ "$fail" = 0 ] && echo "  ok    a composted rw---- file stays rw----; saucier is refused it and the compost's index" \
    || echo "  FAIL  the compost's modes"
exit $fail

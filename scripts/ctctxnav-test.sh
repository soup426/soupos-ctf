#!/usr/bin/env bash
# ctctxnav-test.sh - a files window's right-click menu does not outlive the
# listing it was for (v0.60.317, review R-C28). The keys (the arrows, Enter)
# never closed it, and its labels and actions read the row it was opened on
# out of whatever the window showed next: armed on "remove note.txt", Down
# and Enter into ~/compost, and one click on the menu emptied the compost,
# asked once. Here saucier, with note.txt and compost/precious.txt, does
# just that: nothing is emptied and precious.txt is still on the disk.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctctxnav.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mapfile -d '' -t keys < <(python3 - <<'PY'
out = ['headchef', 'rosemary', 'WAIT:1', 'hire saucier', 'WAIT:1', 'basil', 'WAIT:1', 'basil', 'WAIT:1', 'clockout', 'WAIT:2',
       'saucier', 'basil', 'WAIT:2', 'stir note.txt hello', 'WAIT:1', 'mkbowl compost', 'WAIT:1',
       'stir compost/precious.txt keep me', 'WAIT:1', 'countertop', 'WAIT:2'] + ['MON:mouse_move -150 -150'] * 8
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
go(350, 12); out += click                          # Files at /
go(700, row(1)); out += twice                      # home
go(700, row(1)); out += twice                      # saucier: .., compost/, note.txt
go(700, row(2)); out += right                      # the menu on note.txt
go(710, row(2) + 2 + 22 * 1 + 11); out += click    # remove note.txt, once: armed
out += ['KEY:down', 'WAIT:1', 'KEY:ret', 'WAIT:2'] # the keys: into ~/compost
go(710, row(2) + 2 + 22 * 0 + 11); out += click    # where the menu's first entry was
out += ['WAIT:2']
print('\0'.join(out), end='')
PY
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/l"
grep 'compost emptied' "$WORK/l" >/dev/null && { echo "  FAIL  the compost was emptied by one click after the keys: $(grep -o 'compost emptied[^;]*' "$WORK/l" | head -1)"; fail=1; } \
    || echo "  ok    the menu armed on remove did not empty the compost after Down and Enter"
[ "$(mcopy -n -i "$WORK/disk.img" ::/home/saucier/compost/precious.txt - 2>/dev/null | tr -d '\r\n')" = "keep me" ] \
    && echo "  ok    compost/precious.txt is still on the disk" || { echo "  FAIL  compost/precious.txt is gone"; fail=1; }
[ "$fail" = 0 ] && echo "ctctxnav-test: PASS" || echo "ctctxnav-test: FAIL"
exit "$fail"

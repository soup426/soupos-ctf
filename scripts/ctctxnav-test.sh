#!/usr/bin/env bash
# ctctxnav-test.sh - a files window's right-click menu does not outlive the
# listing it was for (v0.60.317, review R-C28). The keys (the arrows, Enter)
# never closed it, and its labels and actions read the row it was opened on
# out of whatever the window showed next: armed on "remove note.txt", Down
# and Enter into ~/compost, and one click on the menu emptied the compost,
# asked once. Here saucier, with note.txt and four files in compost/, does
# just that: nothing is emptied and they are all still on the disk.
# (v0.60.324, reviews R-C32, R-T48, R-T49) Each of the fix's three parts
# saved the old test alone, and both its checks were absences, so keys or
# clicks that missed passed too. Now: four files in the compost, so the
# menu's row 2 is still in its listing and the row check cannot save it; a
# case with no Enter after it (armed, Down, the same click: the key alone
# must close the menu, or note.txt goes); and positive markers, the listings'
# "files in" lines in order and, last, the menu's remove used as meant,
# which composts note.txt.
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
       'stir compost/precious.txt keep me', 'WAIT:1', 'stir compost/a.txt keep a', 'WAIT:1',
       'stir compost/b.txt keep b', 'WAIT:1', 'stir compost/c.txt keep c', 'WAIT:1',
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
item = lambda i: row(2) + 2 + 22 * i + 11          # the menu opened on row 2: its entry i
go(350, 12); out += click                          # Files at /
go(700, row(1)); out += twice                      # home
go(700, row(1)); out += twice                      # saucier: .., compost/, note.txt
# Armed on remove note.txt, then the keys into ~/compost (Down: compost/),
# and a click where the menu's first entry was ("empty the compost").
go(700, row(2)); out += right
go(710, item(1)); out += click                     # remove note.txt, once: armed
out += ['KEY:down', 'WAIT:1', 'KEY:ret', 'WAIT:2']
go(710, item(0)); out += click
# Back up (the click selected a row: Up to .., Enter).
out += ['KEY:up', 'WAIT:1'] * 5 + ['KEY:ret', 'WAIT:2']
# No Enter: armed again, Down, the same click. The key closes the menu; the
# click lands past the listing's last row and does nothing.
go(700, row(2)); out += right
go(710, item(1)); out += click
out += ['KEY:down', 'WAIT:1']
go(710, item(1)); out += click
# Into the compost and back: 5 entries if note.txt stayed, 6 if that click
# composted it (its "composted" line is the same as the one below).
out += ['KEY:up', 'WAIT:1'] * 3 + ['KEY:down', 'WAIT:1', 'KEY:ret', 'WAIT:2'] + ['KEY:up', 'WAIT:1'] * 5 + ['KEY:ret', 'WAIT:2']
# Last, remove note.txt as meant: the positive control that the clicks land.
go(700, row(2)); out += right
go(710, item(1)); out += click
go(710, item(1)); out += click
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
python3 - "$WORK/l" <<'PY' || fail=1
import re, sys
t = open(sys.argv[1], errors='replace').read()
ev = re.findall(r'\[countertop\] (files in \S+, \d+ entries|composted \S+ as \S+|compost emptied[^\n]*)', t)
print('        ' + '; '.join(ev))
want = ['files in /home, ', 'files in /home/saucier, 3 entries', 'files in /home/saucier/compost, 5 entries',
        'files in /home/saucier, 3 entries', 'files in /home/saucier/compost, 5 entries',
        'files in /home/saucier, 3 entries', 'composted /home/saucier/note.txt as /home/saucier/compost/note.txt']
got = [e for e in ev if not e.startswith('files in /home, ')]
ok = got == want[1:] and any(e.startswith(want[0]) for e in ev)
print(('  ok    ' if ok else '  FAIL  ') + 'in order: saucier (3), the keys into the compost (5), back (3), the compost again (5: the no-Enter case kept note.txt), back (3), note.txt composted as meant; nothing emptied')
sys.exit(0 if ok else 1)
PY
for f in precious a b c; do
    want="keep me"; [ "$f" = precious ] || want="keep $f"
    [ "$(mcopy -n -i "$WORK/disk.img" "::/home/saucier/compost/$f.txt" - 2>/dev/null | tr -d '\r\n')" = "$want" ] \
        || { echo "  FAIL  compost/$f.txt is gone"; fail=1; gone=1; }
done
[ "${gone:-0}" = 0 ] && echo "  ok    the compost's four files are still on the disk"
[ "$(mcopy -n -i "$WORK/disk.img" ::/home/saucier/compost/note.txt - 2>/dev/null | tr -d '\r\n')" = "hello" ] \
    && echo "  ok    compost/note.txt is there: the menu's remove landed" \
    || { echo "  FAIL  compost/note.txt is not there: the menu's remove never landed"; fail=1; }
[ "$fail" = 0 ] && echo "ctctxnav-test: PASS" || echo "ctctxnav-test: FAIL"
exit "$fail"

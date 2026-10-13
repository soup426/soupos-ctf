#!/usr/bin/env bash
# ctdropmenu-test.sh - a drop onto a files window closes its right-click
# menu (v0.60.325, review R-C31). The drop reloads the listing, and the
# menu read its row out of the new one: as saucier, with b.txt after the
# slot a strained x.txt left, HELLO.TXT held in Files 2 (at /), a
# right-click on b.txt in Files 1 (saucier's home), then the drop there:
# HELLO.TXT took x.txt's slot, so the menu's row was HELLO.TXT, and
# "remove" clicked twice composted it, not b.txt. Now the drop closes the
# menu and the two clicks do nothing; then b.txt removed as meant, the
# positive control that the menu's clicks land.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctdropmenu.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mapfile -d '' -t keys < <(python3 - <<'PY'
out = ['headchef', 'rosemary', 'WAIT:1', 'hire saucier', 'WAIT:1', 'basil', 'WAIT:1', 'basil', 'WAIT:1', 'clockout', 'WAIT:2',
       'saucier', 'basil', 'WAIT:2', 'stir x.txt gone', 'WAIT:1', 'stir b.txt keep b', 'WAIT:1', 'strain x.txt', 'WAIT:1',
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
# Files 1 at 604,60 (rows from y 112), Files 2 at 576,88 (rows from y 140),
# as ctfilecopy-test; x 970 is Files 1's alone. A menu is 260 wide, so one
# opened at x 970 is drawn from 764; its entry i is 2 + 22 i + 11 down.
go(350, 12); out += click                          # Files 1 at /
go(700, 138); out += twice; out += twice           # home, saucier: .., b.txt
go(350, 12); out += click                          # Files 2 at /
go(700, 184); out += ['MON:mouse_button 1', 'WAIT:1']   # HELLO.TXT (its row 2), held
go(970, 300)                                       # over Files 1
go(970, 139); out += ['MON:mouse_button 3', 'WAIT:1']   # the right button too: the menu on b.txt (row 1)
out += ['MON:mouse_button 0', 'WAIT:2']            # let go: the drop
go(800, 139 + 2 + 22 * 1 + 11); out += click       # remove, once: armed
out += click                                       # and again
# The positive control: b.txt removed as meant (its row 2 after the drop).
go(970, 157); out += right
go(800, 157 + 2 + 22 * 1 + 11); out += click
out += click
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
ev = re.findall(r'\[countertop\] (files in \S+, \d+ entries|copied \S+ to \S+|composted \S+ as \S+)', t)
print('        ' + '; '.join(ev))
want = ['files in /home/saucier, 2 entries', 'copied /HELLO.TXT to /home/saucier/HELLO.TXT',
        'composted /home/saucier/b.txt as /home/saucier/compost/b.txt']
got = [e for e in ev if not e.startswith('files in /home, ')]
ok = got == want
print(('  ok    ' if ok else '  FAIL  ') + 'in order: saucier (2), HELLO.TXT dropped there with the menu up, then only b.txt composted, as meant')
sys.exit(0 if ok else 1)
PY
[ "$(mcopy -n -i "$WORK/disk.img" ::/home/saucier/HELLO.TXT - 2>/dev/null | head -c 64)" = "$(mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT - 2>/dev/null | head -c 64)" ] \
    && echo "  ok    the dropped HELLO.TXT is still in saucier's home" || { echo "  FAIL  /home/saucier/HELLO.TXT is gone"; fail=1; }
[ "$fail" = 0 ] && echo "ctdropmenu-test: PASS" || echo "ctdropmenu-test: FAIL"
exit "$fail"

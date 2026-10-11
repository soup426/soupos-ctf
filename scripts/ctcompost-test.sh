#!/usr/bin/env bash
# ctcompost-test.sh - compost (v0.60.189), as saucier, whose ~/compost
# already holds an older note.txt (made at the prompt). note.txt and
# other.txt removed in the files window go there, note.txt as note.txt.2
# since the name is taken; note.txt.2's "put back" returns it home with
# its words; "empty the compost" (asked twice) takes the other two off the
# disk. The disk after, the rows read back, the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcompost.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mapfile -d '' -t keys < <(python3 - "$WORK" <<'PY'
import sys
work = sys.argv[1]
out = ['headchef', 'rosemary', 'WAIT:1', 'hire saucier', 'WAIT:1', 'basil', 'WAIT:1', 'basil', 'WAIT:1', 'clockout', 'WAIT:2',
       'saucier', 'basil', 'WAIT:2', 'stir note.txt hello compost', 'WAIT:1', 'stir other.txt goes for good', 'WAIT:1',
       'mkbowl compost', 'WAIT:1', 'stir compost/note.txt an older note', 'WAIT:1',
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
go(700, row(1)); out += twice                      # home
go(700, row(1)); out += twice                      # saucier: .., compost/, note.txt, other.txt
menu(2, 1, 2)                                      # remove note.txt (asked twice): .., compost/, other.txt
menu(2, 1, 2)                                      # remove other.txt: .., compost/
go(700, row(1)); out += twice                      # into the compost
go(1000, 700); out += [f'MON:screendump {work}/s1.ppm', 'WAIT:1']
menu(2, 1)                                         # put back note.txt.2
menu(4, 0, 2)                                      # below the rows: empty the compost (asked twice)
go(1000, 700); out += [f'MON:screendump {work}/s2.ppm', 'WAIT:1']
print('\0'.join(out), end='')
PY
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re
work, fonth = sys.argv[1], sys.argv[2]
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
shape = {}
for c in range(32, 127):
    shape.setdefault(font[c][:14], chr(c))
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
INK = {0x202020, 0x1E4E8C, 0xF0F0F0}
rows = lambda n: [''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if ppm(n)(615 + c*8 + b, 112 + 18 * i + j) in INK) for j in range(14)), '?') for c in range(12)).rstrip() for i in range(4)]
bad = []
if rows('s1') != ['..', 'note.txt', 'note.txt.2', 'other.txt']: bad.append(f'the compost: {rows("s1")}')
if rows('s2') != ['..', '', '', '']: bad.append(f'emptied: {rows("s2")}')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
mcopy -n -i "$WORK/disk.img" "::/home/saucier/note.txt" "$WORK/note" 2>/dev/null && grep -q 'hello compost' "$WORK/note" \
    || { echo "        note.txt is not home with its words"; fail=1; }
for f in /home/saucier/other.txt /home/saucier/compost/other.txt /home/saucier/compost/note.txt /home/saucier/compost/note.txt.2; do
    mdir -i "$WORK/disk.img" "::$f" >/dev/null 2>&1 && { echo "        $f is still on the disk"; fail=1; }
done
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] (composted|put back|compost emptied|could not|may not|not known)' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] composted /home/saucier/note.txt as /home/saucier/compost/note.txt.2' \
       '[countertop] composted /home/saucier/other.txt as /home/saucier/compost/other.txt' \
       '[countertop] put back note.txt.2 to /home/saucier/note.txt' '[countertop] compost emptied, 2 files gone' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    compost: two files removed to ~/compost, note.txt as note.txt.2 (taken); put back with its words; the compost emptied, both others off the disk" \
    || echo "  FAIL  compost"
exit $fail

#!/usr/bin/env bash
# prodwin-test.sh - the window calls, attacked (v0.60.150). Two copies of
# prod.elf start in the background at the console and wait for the
# desktop. victim opens a green window and waits; limits opens its own,
# then asks for windows of no size and too big, with a title in the
# kernel and one running off its memory, puts pixels from above its
# break, from the kernel and half past its break, asks for events into
# the kernel and above its break, uses ids 8 and -1 and every id but its
# own (the victim's among them), then closes its own twice. Every attack
# gives -1, the victim's window stays green, and once the desktop has
# left the victim's put, event and open give -1 and its close frees it.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-prodwin.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
keys=("headchef" "rosemary" "WAIT:1" "cook prod.elf victim &" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "cook prod.elf limits > /LIMITS.OUT" "UNTIL:/prod.elf exited with code" "WAIT:1"
      "MON:mouse_move 300 734" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "MON:mouse_move -212 -734" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:4"
      "slurp PRODWIN-DONE" "UNTIL:PRODWIN-DONE")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import sys, re
work = sys.argv[1]
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1').replace('\r', '')
log = re.sub(r'\[(countertop|demand|proc \d+)\][^\n]*\n', '', log)     # kernel lines land mid-line
said = re.findall(r'prod: [^\n]*', log)                                # a prompt can come first on the line
import subprocess
out = subprocess.run(['mcopy', '-n', '-i', f'{work}/disk.img', '::/LIMITS.OUT', '-'], capture_output=True).stdout.decode('latin-1')
said = said[:1] + out.split('\n')[:-1] + said[1:]             # victim's first line, limits', victim's last four
want = ['prod: victim window open',
        'prod: open 0x10 gave -1', 'prod: open 641x10 gave -1', 'prod: open 10x481 gave -1', 'prod: open -1x10 gave -1',
        'prod: open, a kernel title gave -1', 'prod: open, a title running off its memory gave -1',
        'prod: put from above the break gave -1', 'prod: put from the kernel gave -1', 'prod: put half past the break gave -1',
        'prod: event into the kernel gave -1', 'prod: event above the break gave -1',
        'prod: put to id 8 gave -1', 'prod: put to id -1 gave -1', "prod: others' ids: 7 tried, all -1",
        'prod: put to its own gave 0', 'prod: close its own gave 0', 'prod: close it again gave -1',
        'prod: after the desktop, put gave -1', 'prod: after the desktop, event gave -1',
        'prod: after the desktop, close gave 0', 'prod: after the desktop, open gave -1']
bad = []
if said != want:
    for w in want:
        if w not in said: bad.append(f'missing: {w}')
    for s in said:
        if s not in want: bad.append(f'unexpected: {s}')
    if not bad: bad.append('the lines came in another order: ' + ' | '.join(said))
d = open(f'{work}/s1.ppm', 'rb').read(); i = 0; words = []
while len(words) < 4:
    while d[i:i+1].isspace(): i += 1
    j = i
    while not d[j:j+1].isspace(): j += 1
    words.append(d[i:j]); i = j
px = d[i + 1:]; W = int(words[1])
at = lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
for x, y in ((250, 100), (250, 180), (300, 150), (330, 185)):      # the victim's body, 241..340 x 93..192
    if at(x, y) != 0x27AE60: bad.append(f'the victim\'s window at {x},{y} is {at(x,y):06x}, not green')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
# the victim's window was up before limits asked for one (so limits'
# tries on others' ids met it), and was never closed while the desktop was
v=$(grep -n 'open prod victim' "$WORK/log" | cut -d: -f1); l=$(grep -n 'asks for a 100x100' "$WORK/log" | sed -n 2p | cut -d: -f1)
[ -n "$v" ] && [ -n "$l" ] && [ "$v" -lt "$l" ] || { echo "        the victim's window was not up before limits asked (lines $v, $l)"; fail=1; }
grep -E '^\[countertop\] program.* prod victim' "$WORK/log" >/dev/null && { echo "        the desktop closed the victim's window"; fail=1; }
grep -i 'panic' "$WORK/log" >/dev/null && { echo "        the kernel panicked"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the window calls, attacked: sizes, titles, pixel and event pointers, others' ids and a second close all give -1; the victim stays green; after the desktop, -1" \
    || echo "  FAIL  the window calls under attack"
exit $fail

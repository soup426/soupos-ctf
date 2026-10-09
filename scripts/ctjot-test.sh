#!/usr/bin/env bash
# ctjot-test.sh - jot in a terminal window (v0.60.154). In Terminal 1 jot
# makes /NOTE.TXT, two lines are typed and Ctrl-S saves them (the status
# bar says so, read back against the font); Esc leaves and the file on the
# disk holds them. jot opened again there, a second jot in Terminal 2 is
# refused (one buffer, one jot); closing Terminal 1 under its jot hangs it
# up, so Terminal 2's jot then opens. From the files window a text file
# goes to jot and anything else to pour (GREET.ELF).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctjot.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
DOWN=(); for i in $(seq 27); do DOWN+=("KEY:down"); done
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "jot /NOTE.TXT" "WAIT:2" "TYPE:cooked in a window" "KEY:ret" "TYPE:second line" "KEY:ctrl-s" "WAIT:2"
      "MON:screendump $WORK/s1.ppm" "WAIT:1" "KEY:esc" "WAIT:2"
      "jot /NOTE.TXT" "WAIT:2" "${CLICK[@]}" "jot /HELLO.TXT" "WAIT:2" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "MON:mouse_move 490 60" "WAIT:1" "${CLICK[@]}" "WAIT:1"
      "jot /HELLO.TXT" "WAIT:2" "MON:screendump $WORK/s3.ppm" "WAIT:1" "KEY:esc" "WAIT:1"
      "MON:mouse_move -340 -60" "WAIT:1" "${CLICK[@]}" "${DOWN[@]}" "KEY:ret" "WAIT:3"
      "MON:mouse_move -62 0" "WAIT:1" "${CLICK[@]}" "WAIT:3" "slurp CTJOT-DONE" "UNTIL:CTJOT-DONE")
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
def term(at, ox, oy, inverse=False):
    rows = []
    for r in range(25):
        line = ''
        for c in range(80):
            bg = at(ox + c*8, oy + r*16 + 15) if inverse else 0     # the cell's last row: no glyph there
            g = bytes(sum(1 << (7 - i) for i in range(8) if at(ox + c*8 + i, oy + r*16 + j) != bg) for j in range(14))
            line += shape.get(g, '?')
        rows.append(line.rstrip())
    return rows
T1, T2 = (61, 83), (89, 111)
bad = []
s1 = term(ppm('s1'), *T1)
if s1[:2] != ['cooked in a window', 'second line']: bad.append('s1: jot shows ' + ' | '.join(s1[:3]))
bar = term(ppm('s1'), *T1, inverse=True)[24]
if 'NOTE.TXT' not in bar or 'Saved.' not in bar: bad.append(f's1: the status bar says "{bar}"')
s2 = term(ppm('s2'), *T2)
if not any('jot is already open somewhere else; one at a time.' in r for r in s2): bad.append('s2: Terminal 2 was not refused: ' + ' | '.join(r for r in s2 if r)[:300])
s3 = term(ppm('s3'), *T2)
if s3[:2] != ['Hello from soupOS!', 'A warm bowl of kernel soup.']: bad.append('s3: Terminal 2 jot shows ' + ' | '.join(s3[:3]))
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
note=$(mcopy -n -i "$WORK/disk.img" ::/NOTE.TXT - 2>/dev/null | od -c | head -3)
[ "$(mcopy -n -i "$WORK/disk.img" ::/NOTE.TXT - 2>/dev/null)" = "$(printf 'cooked in a window\nsecond line')" ] || { echo "        NOTE.TXT on the disk: $note"; fail=1; }
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -x '\[countertop\] pour /GREET.ELF' "$WORK/log" >/dev/null || { echo "        GREET.ELF did not go to pour: $(grep -E 'countertop\] (jot|pour)' "$WORK/log" | tr '\n' ';')"; fail=1; }
grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        Leave did not end the desktop"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    jot in a window: a new file typed and saved (on the disk after), a second jot refused, a closed window frees it, GREET.ELF to pour" \
    || echo "  FAIL  jot in a terminal window"
exit $fail

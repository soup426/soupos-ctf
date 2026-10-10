#!/usr/bin/env bash
# ctzone-test.sh - the clock in the cook's own time (v0.60.187). With
# "zone -5" and "clock 12" in /countertop the bar reads " h:mm am/pm" for
# UTC (QEMU's RTC, the host's) less five hours, and the calendar's today is
# that day; with "zone 14" and 24 hours, "hh:mm:ss" fourteen hours on. The
# minute may have turned between the screendump and the host's look.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctzone.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'zone -5\nclock 12\n' > "$WORK/settings"
mcopy -i "$WORK/disk.img" "$WORK/settings" ::/countertop
L="MON:mouse_move -150 -150"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:3" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 150 12" "MON:mouse_move 150 0" "MON:mouse_move 150 0" "MON:mouse_move 150 0"
      "MON:mouse_move 150 0" "MON:mouse_move 150 0" "MON:mouse_move 90 0" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2"
      "MON:mouse_move -150 150" "WAIT:1" "KEY:esc" "WAIT:2" "stir /countertop zone 14" "WAIT:1" "countertop" "WAIT:3" "MON:screendump $WORK/s2.ppm" "WAIT:1" "KEY:esc" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re, os, datetime
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
clock = lambda n: ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if ppm(n)(952 + c*8 + b, 4 + j) == 0xF0F0F0) for j in range(14)), '?') for c in range(8))
taken = lambda n: datetime.datetime.fromtimestamp(os.path.getmtime(f'{work}/{n}.ppm'), datetime.timezone.utc)
bad = []
def want12(t):
    h = t.hour % 12 or 12
    return f'{h:2d}:{t.minute:02d} {"am" if t.hour < 12 else "pm"}'
t1 = taken('s1') - datetime.timedelta(hours=5)
got = clock('s1')
if got not in (want12(t1), want12(t1 - datetime.timedelta(minutes=1))): bad.append(f'zone -5, 12-hour: "{got}", not "{want12(t1)}"')
m = re.search(r'^\[countertop\] calendar (\d+)-(\d+), today (\d+)$', open(f'{work}/log').read(), re.M)
if not m or tuple(map(int, m.groups())) != (t1.year, t1.month, t1.day): bad.append(f'the calendar: {m.group(0) if m else None}, not {t1.date()}')
t2 = taken('s2') + datetime.timedelta(hours=14)
got = clock('s2')
if got[:5] not in (t2.strftime('%H:%M'), (t2 - datetime.timedelta(minutes=1)).strftime('%H:%M')) or got[2] != ':' or got[5] != ':':
    bad.append(f'zone 14: "{got}", not "{t2.strftime("%H:%M:%S")}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
grep -E '^\[countertop\] settings: zone' "$WORK/log" > "$WORK/events"
printf '%s\n' '[countertop] settings: zone -5, 12-hour clock' '[countertop] settings: zone 14, 24-hour clock' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the cook's own time: zone -5 on a 12-hour clock, and the calendar's today with it; zone 14 on 24 hours" \
    || echo "  FAIL  the cook's own time"
exit $fail

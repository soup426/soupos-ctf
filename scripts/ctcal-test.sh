#!/usr/bin/env bash
# ctcal-test.sh - a calendar under the clock (v0.60.175). A click on the
# clock drops this month: its title, the day names and every week's
# numbers compared with Python's calendar for the date the RTC gave (the
# date the desktop logged, checked against the host's UTC date, which is
# QEMU's RTC), today on blue. Another click on the clock puts it up; open
# again, a click elsewhere puts it up too.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctcal.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 300 12" "MON:mouse_move 300 0" "MON:mouse_move 300 0" "MON:mouse_move 80 0" "WAIT:1"
      "${CLICK[@]}" "MON:screendump $WORK/s1.ppm" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "${CLICK[@]}" "MON:mouse_move -300 300" "MON:mouse_move -180 288" "WAIT:1" "${CLICK[@]}" "MON:screendump $WORK/s3.ppm" "WAIT:1"
      "KEY:esc" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re, calendar, datetime
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
def word(at, x, y, n, ink):
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(x + c*8 + b, y + j) in ink) for j in range(14)), '?')
                   for c in range(n))
bad = []
m = re.search(r'^\[countertop\] calendar (\d+)-(\d+), today (\d+)$', open(f'{work}/log').read(), re.M)
if not m: print('        no calendar line in the log'); sys.exit(1)
y, mo, d = map(int, m.groups())
utc = datetime.datetime.now(datetime.timezone.utc).date()
if abs((datetime.date(y, mo, d) - utc).days) > 1: bad.append(f'the RTC said {y}-{mo}-{d}, the host {utc}')
s1 = ppm('s1')
X, Y = 1024 - 184 - 4, 24                         # the calendar's corner
title = word(s1, X + 8, Y + 4, 20, {0x202020}).rstrip()
if title != f'{calendar.month_name[mo]} {y}': bad.append(f'title "{title}"')
head = word(s1, X + 8, Y + 24, 20, {0x606870})
if head != 'Su Mo Tu We Th Fr Sa': bad.append(f'day names "{head}"')
for r, week in enumerate(calendar.Calendar(firstweekday=6).monthdayscalendar(y, mo)):
    got = ' '.join(word(s1, X + 8 + c * 24, Y + 44 + r * 18, 2, {0x202020, 0xF0F0F0}) for c in range(7))
    want = ' '.join(f'{v:2d}' if v else '  ' for v in week)
    if got != want: bad.append(f'week {r}: "{got}", not "{want}"')
    if d in week:
        c = week.index(d)
        if s1(X + 8 + c * 24 - 1, Y + 44 + r * 18 + 15) != 0x3A6EA5: bad.append(f'today ({d}) not on blue')
if ppm('s2')(X + 100, Y + 80) == 0xF4F1EA: bad.append('a second click on the clock did not put it up')
if ppm('s3')(X + 100, Y + 80) == 0xF4F1EA: bad.append('a click elsewhere did not put it up')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
[ "$(grep -c '^\[countertop\] calendar' "$WORK/log")" = 2 ] || { echo "        not opened twice"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the calendar: this month under the clock, every week matching Python's for the RTC's date, today on blue; the clock and a click away put it up" \
    || echo "  FAIL  the calendar"
exit $fail

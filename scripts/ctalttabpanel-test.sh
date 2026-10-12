#!/usr/bin/env bash
# ctalttabpanel-test.sh - Alt+Tab you can see (v0.60.203). Welcome, Pantry
# and a terminal, the terminal on top. With Alt held, a Tab puts a panel in
# the middle of the screen listing the windows (the top one first) with the
# next one marked; a second Tab marks the third, Welcome; letting go of Alt
# brings the marked one to the top and takes the panel down.
# HMP's sendkey queues a held key's release before what follows, so Alt
# cannot be held across two Tabs there: those keys go through a QMP socket
# as separate down and up events. The panel: screendumps (its frame where
# the panel is, the mark's blue bar on the third row); the names: the log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctalttabpanel.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}"
      "MON:mouse_move 0 400" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" \
    -qmp "unix:$WORK/qmp.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
python3 - "$WORK" <<'PY' || { echo "  FAIL  could not drive QMP"; fail=1; }
import json, socket, sys, time
work = sys.argv[1]
s = socket.socket(socket.AF_UNIX); s.connect(f'{work}/qmp.sock'); f = s.makefile('rw')
def rd():
    while True:
        m = json.loads(f.readline())
        if 'return' in m or 'error' in m: return m
f.readline()                                     # the greeting
def q(cmd, **args):
    f.write(json.dumps({'execute': cmd, 'arguments': args} if args else {'execute': cmd}) + '\n'); f.flush()
    m = rd()
    if 'error' in m: sys.exit('QMP: %r' % m)
q('qmp_capabilities')
def key(name, down):
    q('input-send-event', events=[{'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': name}}}])
def hmp(line): q('human-monitor-command', **{'command-line': line})
key('alt', True); time.sleep(0.3)
key('tab', True); time.sleep(0.1); key('tab', False); time.sleep(1)
hmp(f'screendump {work}/s1.ppm'); time.sleep(0.5)
key('tab', True); time.sleep(0.1); key('tab', False); time.sleep(1)
hmp(f'screendump {work}/s2.ppm'); time.sleep(0.5)
key('alt', False); time.sleep(1.5)
hmp(f'screendump {work}/s3.ppm'); time.sleep(0.5)
PY
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
marks = re.findall(r'\[countertop\] tab panel marks (.*)', log)
check(marks[:2] == ['Pantry', 'Welcome'], 'the mark: Pantry after one Tab, Welcome after two: %r' % marks)
tos = re.findall(r'\[countertop\] alt-tab to (.*)', log)
check(tos and tos[-1].strip() == 'Welcome', 'Alt let go: Welcome on top: %r' % tos)
# The panel, 300 wide, three rows of 24 under a 20-pixel heading, in the
# middle of 1024x768: the frame at its left edge, the mark's blue bar on
# the second row after one Tab and the third after two; gone after Alt.
W, H, TW, IH, HEAD = 1024, 768, 300, 24, 20
ph = HEAD + 3 * IH + 8
px, py = W // 2 - TW // 2, H // 2 - ph // 2
BLUE = 0x3A6EA5
row = lambda i: py + HEAD + 4 + i * IH + IH // 2
s1, s2, s3 = ppm('s1'), ppm('s2'), ppm('s3')
check(s1(px + TW - 8, row(1)) == BLUE and s1(px + TW - 8, row(2)) != BLUE, 'one Tab: the panel, the second row marked')
check(s2(px + TW - 8, row(2)) == BLUE and s2(px + TW - 8, row(1)) != BLUE, 'two Tabs: the third row marked')
check(s3(px + TW - 8, row(2)) != BLUE and s3(px + TW - 8, row(1)) != BLUE, 'Alt let go: the panel is gone')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "ctalttabpanel-test: PASS" || echo "ctalttabpanel-test: FAIL"
exit "$fail"

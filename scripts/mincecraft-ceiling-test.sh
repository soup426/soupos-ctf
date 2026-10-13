#!/usr/bin/env bash
# mincecraft-ceiling-test.sh - flying stops at a ceiling the save allows
# (v0.60.276, review R-M1). Flying had no ceiling, but a world file whose
# cook stands at y 68 (WY + 4) or above is refused on load, and the next
# start made a new world in its place: everything built was lost. The flat
# world; fly on, Space held 14 s (some 70 blocks of climb), P saves. The
# cook stops at the ceiling (y 66), and boot 2 loads the world.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcceil.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1 name, $2 args, $3 the QMP steps (python), then keys after
    local name=$1 args=$2 steps=$3; shift 3
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$name.log" -monitor "unix:$WORK/mon-$name.sock,server,nowait" \
        -qmp "unix:$WORK/qmp-$name.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $args -script -wait -world /FLY.WLD &" "WAIT:1" "countertop" \
        "UNTIL:[mincecraft] window up" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($name)"; fail=1; }
    python3 - "$WORK/qmp-$name.sock" "$steps" <<'QMP' || { echo "  FAIL  could not drive QMP ($name)"; fail=1; }
import json, socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile('rw'); f.readline()
def q(cmd, **a):
    f.write(json.dumps({'execute': cmd, 'arguments': a} if a else {'execute': cmd}) + '\n'); f.flush()
    while True:
        m = json.loads(f.readline())
        if 'return' in m or 'error' in m: return m
def key(n, d): q('input-send-event', events=[{'type': 'key', 'data': {'down': d, 'key': {'type': 'qcode', 'data': n}}}])
def tap(n, hold): key(n, True); time.sleep(hold); key(n, False)
q('qmp_capabilities')
exec(sys.argv[2])
QMP
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU after ($name)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
# two quick taps (fly on), Space held 14 s, a mark, P
boot fly "-flat" "tap('spc', 0.07); time.sleep(0.15); tap('spc', 0.07); time.sleep(0.5)
tap('spc', 14.0); time.sleep(0.5)
tap('m', 0.05); time.sleep(0.5)
tap('p', 0.05); time.sleep(1.5)" "KEY:esc" "WAIT:3"
boot load "" "time.sleep(2.0)" "KEY:esc" "WAIT:3"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
t = open(f'{work}/serial-fly.log', 'rb').read().decode('latin-1')
ys = [int(m.group(1)) / 65536 for m in re.finditer(r'\[mincecraft\] p -?\d+ (-?\d+) -?\d+', t)]
check('[mincecraft] fly on' in t, 'two quick Spaces: fly on')
top = max(ys) if ys else 0
check(bool(ys) and top <= 66.01, 'Space held 14 s: the cook climbed to %.2f, not past the ceiling (66)' % top)
check(bool(ys) and top >= 65.99, 'and reached it: %.2f' % top)
check('[mincecraft] saved /FLY.WLD' in t, 'P saved')
t2 = open(f'{work}/serial-load.log', 'rb').read().decode('latin-1')
check('[mincecraft] loaded /FLY.WLD' in t2, 'boot 2 loaded the world, not a new one')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-ceiling-test: PASS" || echo "mincecraft-ceiling-test: FAIL"
exit "$fail"

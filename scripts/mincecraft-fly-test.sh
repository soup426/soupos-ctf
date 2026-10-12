#!/usr/bin/env bash
# mincecraft-fly-test.sh - flying (v0.60.249). The flat world; over QMP (real
# key downs and ups) Space twice quickly: "fly on"; Space held a second: the
# cook rises; let go: it hangs there; Shift held: it sinks; P saves. Boot 2
# loads it: still flying (the save keeps it), hanging, not falling. A
# single Space is still a jump (boot 1's first tap goes up and lands).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcfly.XXXXXX)
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
# a jump; then two taps 150 ms apart; Space held 1 s; 1 s hanging; Shift held 0.6 s; a mark between each
boot fly "-flat" "tap('spc', 0.08); time.sleep(1.5)
tap('m', 0.05); time.sleep(0.3)
tap('spc', 0.07); time.sleep(0.15); tap('spc', 0.07); time.sleep(0.5)
tap('m', 0.05); time.sleep(0.3)
tap('spc', 1.0); time.sleep(0.3)
tap('m', 0.05); time.sleep(1.0)
tap('m', 0.05); time.sleep(0.3)
tap('shift', 0.6); time.sleep(0.3)
tap('m', 0.05); time.sleep(0.3)
tap('p', 0.05); time.sleep(1.5)" "KEY:esc" "WAIT:3"
boot load "" "time.sleep(2.0)
tap('m', 0.05); time.sleep(1.5)
tap('m', 0.05); time.sleep(0.3)" "KEY:esc" "WAIT:3"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def parts(n):
    t = open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
    return t, re.split(r'\[mincecraft\] mark\n', t)
ys = lambda t: [int(m.group(1)) / 65536 for m in re.finditer(r'\[mincecraft\] p -?\d+ (-?\d+) -?\d+', t)]
t, p = parts('fly')
check(len(p) == 6, 'boot 1: five marks: %d stretches' % len(p))
if len(p) == 6:
    j = ys(p[0])
    check(j and max(j) > j[0] + 0.5 and abs(j[-1] - j[0]) < 0.01, 'one Space is a jump: up %.2f and down again' % ((max(j) - j[0]) if j else 0))
    check(p[1].count('[mincecraft] fly on') == 1, 'two quick Spaces: fly on')
    up = ys(p[2]); hang = ys(p[3]); down = ys(p[4])
    check(up and up[-1] - up[0] > 2, 'Space held: up %.1f blocks' % ((up[-1] - up[0]) if up else 0))
    check(hang and max(hang) - min(hang) < 0.01, 'let go: hanging (%.3f of movement)' % ((max(hang) - min(hang)) if hang else -1))
    check(down and down[0] - down[-1] > 0.5, 'Shift held: down %.1f' % ((down[0] - down[-1]) if down else 0))
    check('[mincecraft] saved /FLY.WLD' in p[5], 'P saved, flying')
t2, q = parts('load')
check('[mincecraft] loaded /FLY.WLD' in t2, 'boot 2: the world loaded')
if len(q) >= 2:
    h = ys(q[1])
    check(h and max(h) - min(h) < 0.01 and h[0] > 12, 'still flying after the load: hanging at %.1f, not falling' % (h[0] if h else 0))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-fly-test: PASS" || echo "mincecraft-fly-test: FAIL"
exit "$fail"

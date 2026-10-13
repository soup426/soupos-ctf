#!/usr/bin/env bash
# mincecraft-fly-test.sh - flying (v0.60.249). The flat world; over QMP (real
# key downs and ups) Space twice quickly: "fly on"; Space held a second: the
# cook rises; let go: it hangs there; Shift held: it sinks; P saves. Boot 2
# loads it: still flying (the save keeps it), hanging, not falling. A
# single Space is still a jump (boot 1's first tap goes up and lands). Boot
# 3 (v0.60.315): a block in the flying cook's cell, and it hangs.
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
# (v0.60.315, review R-M17) A block in the cook's cell while it flies with
# nothing held (sugar falls into it so): move_y ran with no move and snapped
# the feet down a block a tick, through the floor without end. The saved
# world has the cook put on the floor (feet 11.0) with a crumb in its feet's
# cell (grid re-encoded, CRC-32 again); boot 3 loads it: the cook stays at 11
# (each snap put it in the ground, and the next snapped it lower).
mcopy -n -i "$WORK/disk.img" ::/FLY.WLD "$WORK/fly.wld" 2>/dev/null
python3 - "$WORK/fly.wld" <<'EDIT' && mcopy -o -i "$WORK/disk.img" "$WORK/fly.wld" ::/FLY.WLD || { echo "  FAIL  could not put a block in the saved world"; fail=1; }
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); body, crc = d[:-4], struct.unpack('<I', d[-4:])[0]
assert d[:4] == b'MINC' and zlib.crc32(body) == crc, 'not a world file'
x, y, z = (struct.unpack('<i', d[16 + 4 * k:20 + 4 * k])[0] >> 16 for k in range(3))
y = 11                                               # down to the flat world's floor (its top is 11): a snap lands in the ground
body = body[:20] + struct.pack('<i', y << 16) + body[24:]
grid = bytearray(); i = 100
while i < len(body): grid += bytes([body[i + 1]]) * body[i]; i += 2
assert len(grid) == 128 * 128 * 64
grid[(y * 128 + z) * 128 + x] = 2                    # B_CRUMB, in the feet's cell
out = bytearray(body[:100]); i = 0
while i < len(grid):
    n = 1
    while i + n < len(grid) and n < 255 and grid[i + n] == grid[i]: n += 1
    out += bytes([n, grid[i]]); i += n
open(sys.argv[1], 'wb').write(out + struct.pack('<I', zlib.crc32(bytes(out))))
EDIT
boot inside "" "time.sleep(2.0)
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
check(len(q) >= 2, 'boot 2: a mark after the load: %d stretches' % len(q))   # (v0.60.256, R-T6) not a skip
if len(q) >= 2:
    h = ys(q[1])
    check(h and max(h) - min(h) < 0.01 and h[0] > 12, 'still flying after the load: hanging at %.1f, not falling' % (h[0] if h else 0))
t3, r = parts('inside')
check('[mincecraft] loaded /FLY.WLD' in t3 and len(r) >= 2, 'boot 3: the world with a block in the cook loaded, a mark after it')
if len(r) >= 2:
    h = ys(r[1])
    check(h and max(h) - min(h) < 0.01 and min(h) > 10, 'a block in the flying cook, nothing held: it hangs at %.1f (%.2f of movement), not sinking through the floor' % ((h[0], max(h) - min(h)) if h else (0, -1)))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-fly-test: PASS" || echo "mincecraft-fly-test: FAIL"
exit "$fail"

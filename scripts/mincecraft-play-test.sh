#!/usr/bin/env bash
# mincecraft-play-test.sh - Mincecraft's player (v0.60.194) in the flat
# world (crust at y 10, so the feet start at y 11, at x = z = 64.5), from
# the -script markers: "p x y z yaw pitch ground" every tick, 16.16 and
# angle units. "m" prints a mark between the phases:
#   1. W held a second: forward (+z) 3 to 5.5 blocks, on the ground
#   2. space: a jump of 1.1 to 1.5 blocks, back down on the ground
#   3. Alt+Tab elsewhere, W held: nothing moves (the bitmap is zeros);
#      two more Alt+Tabs bring it back (Mincecraft, Pantry, Welcome)
#   4. Esc: bye
# A second boot (v0.60.195) mines and builds from the spawn, where the
# 0.6-wide box sits inside one cell: looking down (pitch -227), F held a
# second breaks the crust underfoot (hardness 6 ticks) into the inventory
# and the cook drops into the hole; r then aims at their own cell and is
# refused; looking up a little, r places it back on top of the next
# block over (tried three times, a little higher each, exactly one lands).
# Screendumps (v0.60.200): looking down, the block aimed at is outlined
# (dark edges, the outline's own colour, away from the hotbar); after the
# break, the count in the first slot has a black shadow under its white.
# It opens on top, so it has the focus without a click. It starts at the
# console with -wait, in the background, before the desktop, so its
# markers reach the serial log.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcplay.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
boot() {   # $1: name, then the keys after the desktop is up
    local n=$1; shift
    cp disk.img "$WORK/disk-$n.img"
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -time 600 -world /PLAY.WLD &" "WAIT:1" "countertop" "WAIT:5" "$@")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk-$n.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$n.log" -monitor "unix:$WORK/mon-$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$n.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($n)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot walk "TYPE:m" "KEY:w 1000" "WAIT:2" \
          "TYPE:m" "KEY:spc 100" "WAIT:3" \
          "TYPE:m" "KEY:alt-tab" "WAIT:1" "KEY:w 1000" "WAIT:2" "KEY:alt-tab" "WAIT:1" "KEY:alt-tab" "WAIT:1" \
          "TYPE:m" "WAIT:1" "KEY:esc" "WAIT:3"
boot build "TYPE:m" "KEY:down 2000" "WAIT:1" "MON:screendump $WORK/look.ppm" "WAIT:1" \
           "TYPE:m" "KEY:f 1000" "WAIT:2" "MON:screendump $WORK/broke.ppm" "WAIT:1" \
           "TYPE:m" "TYPE:r" "WAIT:1" \
           "TYPE:m" "KEY:up 1000" "WAIT:1" "TYPE:r" "WAIT:1" "KEY:up 200" "WAIT:1" "TYPE:r" "WAIT:1" "KEY:up 200" "WAIT:1" "TYPE:r" "WAIT:1" \
           "TYPE:m" "WAIT:1" "KEY:esc" "WAIT:3"
# (v0.60.209) W held down across an Alt+Tab: key down and up events over a
# QMP socket (HMP's sendkey would let W go before the Alt+Tab).
cp disk.img "$WORK/disk-hold.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk-hold.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial-hold.log" -monitor "unix:$WORK/mon-hold.sock,server,nowait" \
    -qmp "unix:$WORK/qmp-hold.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon-hold.sock" "headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -time 600 -world /PLAY.WLD &" "WAIT:1" "countertop" "WAIT:5" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU (hold)"; fail=1; }
python3 - "$WORK/qmp-hold.sock" <<'QMP' || { echo "  FAIL  could not drive QMP (hold)"; fail=1; }
import json, socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); f = s.makefile('rw')
f.readline()
def q(cmd, **a):
    f.write(json.dumps({'execute': cmd, 'arguments': a} if a else {'execute': cmd}) + '\n'); f.flush()
    while True:
        m = json.loads(f.readline())
        if 'return' in m or 'error' in m: return m
def key(n, d): q('input-send-event', events=[{'type': 'key', 'data': {'down': d, 'key': {'type': 'qcode', 'data': n}}}])
q('qmp_capabilities')
key('w', True); time.sleep(1.0)
key('alt', True); time.sleep(0.1); key('tab', True); time.sleep(0.1); key('tab', False); time.sleep(0.1); key('alt', False)
time.sleep(1.0); key('w', False); time.sleep(0.5)
QMP
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1] + '/serial-walk.log', 'rb').read().decode('latin-1')
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
ONE, G = 65536, 11 << 16
phases, cur = [], []
for m in re.finditer(r'\[mincecraft\] (mark|p (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (\d+))', log):
    if m.group(1) == 'mark': phases.append(cur); cur = []
    else: cur.append(tuple(int(v) for v in m.groups()[1:]))
phases.append(cur)
check(len(phases) == 5, 'four marks, five stretches of ticks: %d' % len(phases))
if len(phases) == 5:
    pre, walk, jump, away, end = phases
    check(pre and pre[-1][1] == G and pre[-1][5] == 1, 'spawned on the ground at y 11: %r' % (pre[-1:],))
    check(pre and pre[-1][0] == 64 * ONE + ONE // 2 and pre[-1][2] == 64 * ONE + ONE // 2, 'at x = z = 64.5')
    if walk:
        dz = (walk[-1][2] - walk[0][2]) / ONE
        check(3.0 <= dz <= 5.5, 'W a second: %.2f blocks forward' % dz)
        check(walk[-1][0] == walk[0][0], 'straight ahead: x unchanged')
        check(all(p[1] == G for p in walk), 'on the ground all the way')
    if jump:
        peak = (max(p[1] for p in jump) - G) / ONE
        check(1.1 <= peak <= 1.5, 'a jump of %.2f blocks' % peak)
        check(jump[-1][1] == G and jump[-1][5] == 1, 'back on the ground')
    if away:
        check(away[0][2] == away[-1][2], 'W with the focus elsewhere: not a step (%d to %d)' % (away[0][2], away[-1][2]))
check('[mincecraft] bye' in log, 'Esc ended it')
# The second boot, mining and building.
blog = open(sys.argv[1] + '/serial-build.log', 'rb').read().decode('latin-1')
parts = re.split(r'\[mincecraft\] mark\n', blog)
check(len(parts) == 6, 'build: five marks, six stretches: %d' % len(parts))
def ps(t): return [tuple(int(v) for v in m.groups()) for m in re.finditer(r'\[mincecraft\] p (-?\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+) (\d+)', t)]
if len(parts) == 6:
    _, look, brk, mine, place, end = parts
    check(ps(look) and ps(look)[-1][4] == -227, 'looking down, pitch held at -227: %r' % (ps(look)[-1:],))
    check('[mincecraft] broke 64 10 64 1' in brk, 'F broke the crust underfoot (64 10 64, crust)')
    check(re.search(r'\[mincecraft\] inv 1=1\b', brk), 'into the inventory: inv 1=1')
    check(ps(brk) and ps(brk)[-1][1] == 10 << 16 and ps(brk)[-1][5] == 1, 'and the cook dropped into the hole, on y 10')
    check('[mincecraft] place refused' in mine and 'placed' not in mine, 'r at their own cell: refused')
    pl = re.findall(r'\[mincecraft\] placed (\d+) (\d+) (\d+) (\d+)', place)
    check(len(pl) == 1, 'looking up a little: exactly one placed of three tries: %r' % pl)
    if pl:
        x, y, z, b = map(int, pl[0])
        check(x == 64 and y == 11 and z in (65, 66) and b == 1, 'on top of the next block over: %r' % (pl[0],))
    check(re.search(r'\[mincecraft\] inv 1=0\b', place), 'out of the inventory: inv 1=0')
def ppm(n):
    d = open(f'{sys.argv[1]}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[((93 + y) * W + 241 + x) * 3] << 16) | (px[((93 + y) * W + 241 + x) * 3 + 1] << 8) | px[((93 + y) * W + 241 + x) * 3 + 2]
look = ppm('look')
edge = sum(1 for y in range(0, 420, 2) for x in range(0, 640, 2) if look(x, y) == 0x1A1208)
check(edge >= 50, 'looking down, the block aimed at is outlined: %d outline pixels' % edge)
br = ppm('broke')
slot = [br(x, y) for x in range(124, 160) for y in range(434, 470)]
check(0xFFFFFF in slot and 0x000000 in slot, 'the count in the first slot: white over a black shadow')
hl = open(sys.argv[1] + '/serial-hold.log', 'rb').read().decode('latin-1')
cut = hl.find('alt-tab to')
zs = lambda t: [int(m.group(1)) for m in re.finditer(r'\[mincecraft\] p -?\d+ -?\d+ (-?\d+)', t)]
before, after = (zs(hl[:cut]), zs(hl[cut:])) if cut > 0 else ([], [])
check(cut > 0 and before and before[-1] - before[0] > 2 * 65536, 'W held: walking before the Alt+Tab (%.1f blocks)' % ((before[-1] - before[0]) / 65536 if before else 0))
check(len(after) > 10 and len(set(after[2:])) == 1, 'and still held after it: stopped within two ticks (%d ticks after, %d places)' % (len(after), len(set(after[2:])) if after else 0))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-play-test: PASS" || echo "mincecraft-play-test: FAIL"
exit "$fail"

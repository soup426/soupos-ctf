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
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -world /PLAY.WLD &" "WAIT:1" "countertop" "WAIT:5"
      "TYPE:m" "KEY:w 1000" "WAIT:2"
      "TYPE:m" "KEY:spc 100" "WAIT:3"
      "TYPE:m" "KEY:alt-tab" "WAIT:1" "KEY:w 1000" "WAIT:2" "KEY:alt-tab" "WAIT:1" "KEY:alt-tab" "WAIT:1"
      "TYPE:m" "WAIT:1" "KEY:esc" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], 'rb').read().decode('latin-1')
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
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-play-test: PASS" || echo "mincecraft-play-test: FAIL"
exit "$fail"

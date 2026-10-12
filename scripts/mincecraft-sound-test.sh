#!/usr/bin/env bash
# mincecraft-sound-test.sh - Mincecraft's sounds through SYS_TONE (v0.60.210),
# with a sound card (AC97); the mixer logs each tone. The flat world:
#   splash: W two seconds into the broth pool dead ahead: 300 Hz, 120 ms
#   mining: looking down, F held on the crust underfoot: a tick (330 Hz,
#           20 ms) every fourth tick held, then the break, lower the harder
#           the block (220 - 4 x hardness: crust 196 Hz), 80 ms
#   placing: looking up a little, r: a plop, 523 Hz, 60 ms
# With no sound card there are no tones and nothing else changes (the other
# Mincecraft tests run without one).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcsound.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
boot() {   # $1: name, then the keys after the desktop is up
    local n=$1; shift
    cp disk.img "$WORK/disk-$n.img"
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf -flat -script -wait -time 600 -world /S.WLD &" "WAIT:1" "countertop" "WAIT:5" "$@")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk-$n.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none -audiodev none,id=a0 -device AC97,audiodev=a0 \
        -serial "file:$WORK/serial-$n.log" -monitor "unix:$WORK/mon-$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$n.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($n)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot splash "KEY:w 2000" "WAIT:4" "KEY:esc" "WAIT:2"
boot mine "KEY:down 2000" "WAIT:1" "KEY:f 1000" "WAIT:2" "KEY:up 1000" "WAIT:1" "TYPE:r" "WAIT:2" "KEY:esc" "WAIT:2"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def tones(n):
    log = open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
    return [(int(a), int(b)) for a, b in re.findall(r'\[mixer\] tone (\d+) Hz for (\d+) ms', log)], log
t, log = tones('splash')
check((300, 120) in t, 'walking into the broth: a splash, 300 Hz for 120 ms: %r' % t)
check(t.count((300, 120)) == 1, 'one splash, not one a tick: %d' % t.count((300, 120)))
t, log = tones('mine')
check(t.count((330, 20)) >= 1, 'breaking: the ticks while F is held: %d' % t.count((330, 20)))
check((196, 80) in t, 'the crust breaking: 196 Hz for 80 ms: %r' % t)
check('[mincecraft] placed' in log and (523, 60) in t, 'placing: a plop, 523 Hz for 60 ms')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-sound-test: PASS" || echo "mincecraft-sound-test: FAIL"
exit "$fail"

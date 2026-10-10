#!/usr/bin/env bash
# ctnet-test.sh - the network on the bar (v0.60.181). Without a network
# card the bar says "no network". With a card and nothing behind it (a
# QEMU hub port: the boot's DHCP gets no answer) it shows 10.0.2.15 dim,
# the compiled-in address; `plumbing 10.0.2.77` in a desktop terminal
# changes it, and within a few seconds the bar does too, the desktop never
# left. With QEMU's user network the boot leases 10.0.2.15: white. Read
# off the bar against the font.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ctnet.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
L="MON:mouse_move -120 -120"
CLICK=("MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:2")
boot() {                                  # boot <name> <extra qemu args...>
    local n=$1; shift
    cp disk.img "$WORK/$n.img"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/$n.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none "$@" \
        -serial "file:$WORK/$n.log" -monitor "unix:$WORK/$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
}
boot a
python3 scripts/qemu_keys.py "$WORK/a.sock" "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:3" \
    "MON:screendump $WORK/a.ppm" "WAIT:1" "KEY:esc" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (a)"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
boot b -netdev hubport,id=n0,hubid=0 -device rtl8139,netdev=n0
python3 scripts/qemu_keys.py "$WORK/b.sock" "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:3" \
    "MON:screendump $WORK/b1.ppm" "WAIT:1" "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 200 12" "WAIT:1" "${CLICK[@]}" \
    "plumbing 10.0.2.77" "WAIT:5" "MON:screendump $WORK/b2.ppm" "WAIT:1" "clockout" "WAIT:2" "KEY:esc" "WAIT:2" \
    >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (b)"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
boot c -netdev user,id=n0 -device rtl8139,netdev=n0
python3 scripts/qemu_keys.py "$WORK/c.sock" "headchef" "rosemary" "WAIT:1" "countertop" "WAIT:3" \
    "MON:screendump $WORK/c.ppm" "WAIT:1" "KEY:esc" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU (c)"; fail=1; }
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
NET_END = 1024 - 64 - 12 - 12                     # the network ends here, right-aligned
def net(n, ink):
    at = ppm(n)
    return ''.join(shape.get(bytes(sum(1 << (7 - b) for b in range(8) if at(NET_END - 128 + c*8 + b, 4 + j) == ink) for j in range(14)), '?')
                   for c in range(16)).strip()
DIM, WHITE = 0xA0A8B0, 0xF0F0F0
bad = []
if net('a', DIM) != 'no network': bad.append(f'a: the bar says "{net("a", DIM)}"')
if net('b1', DIM) != '10.0.2.15': bad.append(f'b1: dim "{net("b1", DIM)}"')
if net('b1', WHITE) != '': bad.append(f'b1: white "{net("b1", WHITE)}"')
if net('b2', DIM) != '10.0.2.77': bad.append(f'b2: dim "{net("b2", DIM)}"')
if net('c', WHITE) != '10.0.2.15': bad.append(f'c: white "{net("c", WHITE)}", dim "{net("c", DIM)}"')
for b in bad: print('        ' + b)
sys.exit(1 if bad else 0)
PY
for n in a b c; do tr -d '\r' < "$WORK/$n.log" | grep -E '^\[countertop\] network' ; done > "$WORK/events"
printf '%s\n' '[countertop] network: no network' '[countertop] network: 10.0.2.15 (not leased)' '[countertop] network: 10.0.2.77 (not leased)' '[countertop] network: 10.0.2.15' | cmp -s - "$WORK/events" \
    || { echo "        the events: $(tr '\n' ';' < "$WORK/events")"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    the network on the bar: no network without a card; with no DHCP 10.0.2.15 dim, following plumbing to 10.0.2.77 in place; leased on the user network, white" \
    || echo "  FAIL  the network on the bar"
exit $fail

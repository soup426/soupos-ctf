#!/usr/bin/env bash
# mincecraft-fall-test.sh - sugar falls (v0.60.233). Boot 1 saves the flat
# world; on the host it gets a crumb at (70, 11, 64) on the crust with three
# sugar on it (y 12-14), and the cook at (70.5, 11, 62.5) facing +z, looking
# down at the crumb. Boot 2 holds F: the crumb breaks, the column comes down
# a block at a time (the markers), and P saves: on the host the sugar is at
# y 11-13 and y 14 is air.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcfall.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1: name, $2: arguments, then the keys after the window
    local name=$1 args=$2; shift 2
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $args -script -wait -time 600 -world /F.WLD &" "WAIT:1" "countertop"
                "UNTIL:[mincecraft] window up" "WAIT:2" "$@" "KEY:esc" "WAIT:3")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$name.log" -monitor "unix:$WORK/mon-$name.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($name)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
grid() {   # $1 in, $2 out (or -), the edit as python: g is the grid, head the header
python3 -I - "$@" <<'PY'
import struct, sys, zlib
b = open(sys.argv[1], 'rb').read(); H = 100
assert b[:4] == b'MINC' and struct.unpack('<I', b[4:8])[0] == 5
g = bytearray(); i = H
while len(g) < 128 * 128 * 64: g += bytes([b[i + 1]]) * b[i]; i += 2
at = lambda x, y, z: (y * 128 + z) * 128 + x
if sys.argv[2] == '-':
    print(' '.join('%d:%d' % (y, g[at(70, y, 64)]) for y in range(10, 16)))
else:
    g[at(70, 11, 64)] = 2                         # B_CRUMB
    for y in (12, 13, 14): g[at(70, y, 64)] = 6   # B_SUGAR
    head = bytearray(b[:H])
    head[16:28] = struct.pack('<iii', (70 << 16) + 32768, 11 << 16, (62 << 16) + 32768)
    head[28:36] = struct.pack('<ii', 0, -82)     # +z, looking down at the crumb
    out = bytearray(head); i = 0
    while i < len(g):
        n = 1
        while i + n < len(g) and n < 255 and g[i + n] == g[i]: n += 1
        out += bytes([n, g[i]]); i += n
    out += struct.pack('<I', zlib.crc32(bytes(out)))
    open(sys.argv[2], 'wb').write(out)
PY
}
boot made "-flat" "TYPE:p" "WAIT:2"
mcopy -n -i "$WORK/disk.img" ::F.WLD "$WORK/base.wld" 2>/dev/null || { echo "  FAIL  no /F.WLD saved"; fail=1; }
grid "$WORK/base.wld" "$WORK/f.wld" || { echo "  FAIL  could not make the sugar world"; fail=1; }
mcopy -o -i "$WORK/disk.img" "$WORK/f.wld" ::F.WLD
# F held 450 ms: the crumb takes 6 ticks (300 ms); the sugar that lands in
# its place would take 5 more after its fall, so it is not broken too.
boot fall "" "KEY:f 450" "WAIT:3" "TYPE:p" "WAIT:2"
mcopy -n -i "$WORK/disk.img" ::F.WLD "$WORK/after.wld" 2>/dev/null
col=$(grid "$WORK/after.wld" - 2>/dev/null)
python3 - "$WORK" "$col" <<'PY' || fail=1
import re, sys
work, col = sys.argv[1], sys.argv[2]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
log = open(f'{work}/serial-fall.log', 'rb').read().decode('latin-1')
broke = re.findall(r'\[mincecraft\] broke [^\n]*', log)
check(broke == ['[mincecraft] broke 70 11 64 2'], 'the crumb under the sugar broken, and nothing else: %r' % broke)
fell = re.findall(r'\[mincecraft\] fell 70 (\d+) 64', log)
check(fell == ['11', '12', '13'], 'the column fell a block at a time, the bottom first, each sugar once: landed at %r' % fell)
check(col == '10:1 11:6 12:6 13:6 14:0 15:0', 'saved: crust at 10, sugar at 11-13, air at 14: %r' % col)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-fall-test: PASS" || echo "mincecraft-fall-test: FAIL"
exit "$fail"

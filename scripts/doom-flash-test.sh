#!/usr/bin/env bash
# doom-flash-test.sh - the pistol and its muzzle flash (v0.60.225). Neither
# had ever been seen: weapons were drawn from (160, 168) where Doom's offsets
# are from (1, 16), so the gun's top was at y 274, under the screen; and the
# flash was the lump PISFB0, which DOOM1.WAD has not (it is PISFA0). E1M1's
# opening frame: the pistol (PISGA0, decoded here from the WAD through
# PLAYPAL) where Doom puts it, x 127, y 122 down to the status bar. Then
# five shots (Ctrl held, one each 15 ticks): each logs the frame its flash
# was first drawn in, within the flash's 8 ticks of the shot.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomflash.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:2" "doom" "WAIT:6" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:4"
      "MON:screendump $WORK/still.ppm" "WAIT:1" "KEY:ctrl 700" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
log = open(f'{work}/serial.log', 'rb').read().decode('latin-1')
shots = [int(t) for t in re.findall(r'\[doom\] fire ammo=\d+ tick=(\d+)', log)]
flashes = [int(t) for t in re.findall(r'\[doom\] flash tick=(\d+)', log)]
check(len(shots) >= 4, 'Ctrl held fired: %d shots' % len(shots))
late = [s for s in shots if not any(s <= f <= s + 8 for f in flashes)]
check(shots and not late, 'every shot\'s flash drawn within 8 ticks: %d flashes for %d shots, none for %r' % (len(flashes), len(shots), late))
import struct
wad = open('DOOM1.WAD', 'rb').read(); n, off = struct.unpack('<4xII', wad[:12])
lump = {}
for i in range(n):
    p, sz, nm = struct.unpack('<II8s', wad[off + 16 * i:off + 16 * i + 16]); lump[nm.rstrip(b'\0').decode()] = wad[p:p + sz]
pal = lump['PLAYPAL'][:768]
g = lump['PISGA0']; w, h, lo, to = struct.unpack('<hhhh', g[:8])
x0, y0 = 1 - lo, 16 - to
d = open(f'{work}/still.ppm', 'rb').read(); hd = d.split(b'\n', 3); W = int(hd[1].split()[0]); px = hd[3]
seen = same = 0
for c in range(w):
    q = struct.unpack('<I', g[8 + 4 * c:12 + 4 * c])[0]
    while g[q] != 0xFF:
        top, cnt = g[q], g[q + 1]
        for k in range(cnt):
            x, y = x0 + c, y0 + top + k
            if y < 168:                               # above the status bar
                i = ((84 + y * 3 + 1) * W + 32 + x * 3 + 1) * 3
                want = pal[3 * g[q + 3 + k]:3 * g[q + 3 + k] + 3]
                seen += 1; same += all(abs(a - b) <= 12 for a, b in zip(px[i:i + 3], want))
        q += cnt + 4
check(seen > 1000 and same >= seen * 95 // 100, 'the pistol on screen where Doom puts it: %d of its %d pixels above the status bar' % (same, seen))
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "doom-flash-test: PASS" || echo "doom-flash-test: FAIL"
exit "$fail"

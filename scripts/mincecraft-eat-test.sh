#!/usr/bin/env bash
# mincecraft-eat-test.sh - eating (v0.60.224). Boot 1 saves the flat world;
# on the host it is given 4 hearts and in hand 7 crust, 2 sugar, 2 florets
# and a butter (the hotbar in id order: crust, sugar, floret, butter). Boot
# 2, e on each in turn: a floret 4 to 7 (the HUD's hearts read back), the
# butter 7 to 9, a floret 9 to 10 (not past ten; still eaten), sugar at ten
# not eaten ("not hungry"), crust refused ("not food"); the counts in the
# markers as they went.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mceat.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1: name, $2: arguments, then the keys after the window
    local name=$1 args=$2; shift 2
    local keys=("headchef" "rosemary" "WAIT:1" "cook mincecraft.elf $args -script -wait -time 600 -world /E.WLD &" "WAIT:1" "countertop"
                "UNTIL:[mincecraft] window up" "WAIT:2" "$@" "KEY:esc" "WAIT:3")
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$name.log" -monitor "unix:$WORK/mon-$name.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$name.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($name)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot made "-flat" "TYPE:p" "WAIT:2"
mcopy -n -i "$WORK/disk.img" ::E.WLD "$WORK/e.wld" 2>/dev/null || { echo "  FAIL  no /E.WLD saved"; fail=1; }
python3 -I - "$WORK/e.wld" <<'EDIT' || { echo "  FAIL  could not make the hungry world"; fail=1; }
import struct, sys, zlib
p = sys.argv[1]; b = bytearray(open(p, 'rb').read())
assert b[:4] == b'MINC' and struct.unpack('<I', b[4:8])[0] == 4
for bid, n in ((1, 7), (6, 2), (8, 2), (10, 1)):      # crust, sugar, florets, butter
    b[36 + 4 * bid:40 + 4 * bid] = struct.pack('<I', n)
b[92:96] = struct.pack('<I', 4)                       # four hearts
b[-4:] = struct.pack('<I', zlib.crc32(bytes(b[:-4])))
open(p, 'wb').write(b)
EDIT
mcopy -o -i "$WORK/disk.img" "$WORK/e.wld" ::E.WLD
boot eat "" "TYPE:3" "TYPE:e" "WAIT:1" "MON:screendump $WORK/seven.ppm" "WAIT:1" "TYPE:4" "TYPE:e" "WAIT:1" \
            "TYPE:3" "TYPE:e" "WAIT:1" "TYPE:2" "TYPE:e" "WAIT:1" "TYPE:1" "TYPE:e" "WAIT:1"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
log = open(f'{work}/serial-eat.log', 'rb').read().decode('latin-1')
ev = re.findall(r'\[mincecraft\] (ate \w+ hearts=\d+|not hungry|not food|inv [^\n]*)', log)
want = ['inv 1=7 6=2 8=2 10=1', 'ate floret hearts=7', 'inv 8=1', 'ate butter hearts=9', 'inv 10=0',
        'ate floret hearts=10', 'inv 8=0', 'not hungry', 'not food']
check(ev == want, 'the eating, in order: %r' % ev)
def ppm(n):
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W = int(words[1])
    return lambda x, y: (px[(y*W+x)*3], px[(y*W+x)*3+1], px[(y*W+x)*3+2])
img = ppm('seven')
cells = [img(241 + 122 + k * 16 + 7, 93 + 416 + 5) for k in range(10)]
red = [r > 180 and g < 90 and bl < 110 for r, g, bl in cells]
check(red == [True] * 7 + [False] * 3, "after a floret the HUD shows 7 hearts: %r" % red)
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-eat-test: PASS" || echo "mincecraft-eat-test: FAIL"
exit "$fail"

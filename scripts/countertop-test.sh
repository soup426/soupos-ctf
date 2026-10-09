#!/usr/bin/env bash
# countertop-test.sh - the desktop's surface (v0.60.141), from screendumps:
# the bar and "soupOS" in it glyph for glyph against font8x16, the
# background's gradient at several heights, the pointer (outline and fill)
# mid-screen and then where it was moved to with nothing left behind, and
# after Esc the console back (no bar, no gradient) with the shell working.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-countertop.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
L="MON:mouse_move -120 -120"
keys=("headchef" "rosemary" "WAIT:1" "countertop" "WAIT:2" "MON:screendump $WORK/s1.ppm" "WAIT:1"
      "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 300 200" "WAIT:1" "MON:screendump $WORK/s2.ppm" "WAIT:1"
      "KEY:esc" "WAIT:2" "hash FBHELLO" "UNTIL:@soupOS:" "MON:screendump $WORK/s3.ppm" "WAIT:1")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK" src/font8x16.h <<'PY' || fail=1
import sys, re
work, fonth = sys.argv[1], sys.argv[2]
font = [bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', m)) for m in re.findall(r'\{((?:0x[0-9A-Fa-f]{2},?){16})\}', open(fonth).read())]
def ppm(n):
    # the header is four words, then exactly one whitespace byte: split()
    # would also eat pixel bytes that happen to be 0x20 or 0x0a
    d = open(f'{work}/{n}.ppm', 'rb').read(); i = 0; words = []
    while len(words) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        words.append(d[i:j]); i = j
    px = d[i + 1:]; W, H = int(words[1]), int(words[2])
    return W, H, lambda x, y: (px[(y*W+x)*3] << 16) | (px[(y*W+x)*3+1] << 8) | px[(y*W+x)*3+2]
bad = []
BAR, TEXT, TOP, BOT, BH = 0x202428, 0xF0F0F0, 0x1E3A5F, 0xD87A3E, 24
def c_div(a, b): return -((-a) // b) if (a < 0) != (b < 0) and a % b else a // b
def gradc(W, H, y):          # C's integer division truncates toward zero
    span = H - BH - 1; num = y - BH; out = 0
    for s in (16, 8, 0):
        a, b = (TOP >> s) & 255, (BOT >> s) & 255
        out |= (a + c_div((b - a) * num, span)) << s
    return out
W, H, at = ppm('s1')
if (W, H) != (1024, 768): bad.append(f'screen {W}x{H}')
if at(600, 5) != BAR: bad.append(f'bar at 600,5 is {at(600,5):06x}')
for i, ch in enumerate('soupOS'):          # glyph for glyph, at 10,4
    g = font[ord(ch)]
    for j in range(16):
        for k in range(8):
            want = TEXT if (g[j] >> (7 - k)) & 1 else BAR
            if at(10 + i * 8 + k, 4 + j) != want: bad.append(f'"{ch}" pixel {k},{j}'); break
for y in (24, 200, 400, 600, 739):          # 740 and below is the taskbar since v0.60.144
    if at(20, y) != gradc(W, H, y): bad.append(f'gradient at y={y}: {at(20,y):06x} not {gradc(W,H,y):06x}')
if at(512, 384) != 0 or at(513, 386) != 0xFFFFFF: bad.append(f'pointer at mid-screen: {at(512,384):06x} {at(513,386):06x}')
W, H, at = ppm('s2')
if at(300, 200) != 0 or at(301, 202) != 0xFFFFFF or at(301, 205) != 0xFFFFFF: bad.append('pointer at 300,200')
# since v0.60.142 512,384 is inside the Pantry window (its body under the pointer)
if at(512, 384) != 0xE6EEF5 or at(513, 386) != 0xE6EEF5: bad.append(f'the pointer left something at 512,384: {at(512,384):06x}')
W, H, at = ppm('s3')
if at(600, 5) == BAR or at(20, 739) == gradc(W, H, 739) or at(20, 400) == gradc(W, H, 400): bad.append('after Esc the desktop is still there')
for b in bad[:8]: print('        ' + b)
sys.exit(1 if bad else 0)
PY
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^\[countertop\] up 1024x768, 768 pages' "$WORK/log" >/dev/null && grep -x '\[countertop\] down' "$WORK/log" >/dev/null || { echo "        no up/down lines in the log"; fail=1; }
grep 'AlphaSOUP-32' "$WORK/log" >/dev/null || { echo "        the shell did not run hash after Esc"; fail=1; }
[ "$fail" = 0 ] && echo "  ok    countertop draws its surface: the bar with soupOS glyph for glyph, the gradient, the pointer moving with nothing left behind, the console back after Esc" \
    || echo "  FAIL  countertop's surface"
exit $fail

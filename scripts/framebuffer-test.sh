#!/usr/bin/env bash
# Does GRUB's linear framebuffer work, and did we draw into it correctly?
#
# Not part of smoke-test.sh, which boots the TEXT menu entry: the difference
# between the two is entirely grub.cfg's gfxpayload, so testing the other one
# means booting a second image. The kernel is identical in both.
#
# Checks the pattern kernel_main draws at known coordinates, from a screendump,
# so a wrong pitch (which shears the picture down the screen) or a wrong colour
# order fails here rather than looking plausible.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-fb.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-fb.XXXXXX.sock)
SHOT=$(mktemp -u /tmp/soupos-fb.XXXXXX.ppm)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$SHOT"; }
trap cleanup EXIT

make iso-fb >/dev/null 2>&1 || { echo "  FAIL  could not build the framebuffer ISO"; exit 1; }

qemu-system-i386 -accel kvm -cpu host \
    -drive file=disk.img,format=raw,if=ide,index=0 \
    -cdrom soupOS-fb.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 6

python3 - "$MON" "$SHOT" >/dev/null 2>&1 <<'PYQ'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.4); s.recv(65536)
s.sendall(("screendump " + sys.argv[2] + "\n").encode()); time.sleep(1.5)
PYQ

grep -E "^\[fb\]" "$LOG" | sed 's/^/  /'

python3 - "$SHOT" <<'PYCHECK'
import sys
try:
    f = open(sys.argv[1], "rb")
    assert f.readline().strip() == b"P6"
    line = f.readline()
    while line.startswith(b"#"): line = f.readline()
    w, h = map(int, line.split()); f.readline()
    d = f.read()
    def px(x, y):
        o = (y * w + x) * 3
        return tuple(d[o:o + 3])
    want = [("left band", w // 6, h // 4, (0xC0, 0x10, 0x10)),
            ("middle band", w // 2, h // 8, (0x10, 0xC0, 0x10)),
            ("right band", 5 * w // 6, h // 4, (0x10, 0x10, 0xC0)),
            ("centre square", w // 2, h // 2, (0xFF, 0xFF, 0xFF))]
    bad = [n for n, x, y, c in want if any(abs(a - b) > 2 for a, b in zip(px(x, y), c))]
    if bad:
        print("  FAIL  wrong pixels: " + ", ".join(bad)); sys.exit(1)
    print(f"  ok    {w}x{h} framebuffer drew the expected pattern")
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  could not read the screendump: {e}"); sys.exit(1)
PYCHECK

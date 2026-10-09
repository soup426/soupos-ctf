#!/usr/bin/env bash
# Log in through the rendered framebuffer console, type a command, and verify
# the glyphs from a screendump - pixel-identically against the same font the
# kernel embeds. This runs against the DEFAULT image, which has been the
# framebuffer build since v0.14.0.
#
# The smoke gate drives the same image and sees everything over the serial
# mirror, so it does not care which console is drawing; this is the only test
# that checks pixels actually reach the screen.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-fb.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-fb.XXXXXX.sock)
SHOT=$(mktemp -u /tmp/soupos-fb.XXXXXX.ppm)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$TESTDISK" "$LOG" "$SHOT"; }
trap cleanup EXIT

# A throwaway copy of the disk, for two reasons: an interactive QEMU window
# holding disk.img must not block the tests (the image is write-locked), and
# a hermetic disk means no state leaks between runs - stale files in the
# image have caused a flaky check before.
TESTDISK=$(mktemp /tmp/soupos-testdisk.XXXXXX.img)
cp disk.img "$TESTDISK"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5

python3 scripts/qemu_keys.py "$MON" \
    "headchef" "rosemary" "WAIT:1" \
    "wash" "WAIT:1" "hash FBHELLO" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }

python3 - "$MON" "$SHOT" >/dev/null 2>&1 <<'PYQ'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.4); s.recv(65536)
s.sendall(("screendump " + sys.argv[2] + "\n").encode()); time.sleep(1.5)
PYQ

grep -E "^\[fb\]|^\[fbcon\]" "$LOG" | sed 's/^/  /'

python3 - "$SHOT" <<'PYCHECK'
import gzip, struct, sys
try:
    d = gzip.open("/usr/share/kbd/consolefonts/default8x16.psfu.gz", "rb").read()
    hdr = struct.unpack("<8I", d[:32]); glyphs = d[hdr[2]:]
    def glyph(ch): return glyphs[ord(ch)*16:(ord(ch)+1)*16]

    f = open(sys.argv[1], "rb")
    assert f.readline().strip() == b"P6"
    line = f.readline()
    while line.startswith(b"#"): line = f.readline()
    w, h = map(int, line.split()); f.readline()
    img = f.read()

    FG, BG = (0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00)
    text = "hash FBHELLO"
    def strip_row(y):
        out = bytearray()
        for ch in text:
            bits = glyph(ch)[y]
            for x in range(8):
                out += bytes(FG if bits & (0x80 >> x) else BG)
        return bytes(out)

    # Anchor on the glyphs' MID-HEIGHT row: every letter has ink there. The
    # top row of most letters is blank, and anchoring on an all-black row
    # matches the first black run in the image instead of the text.
    ANCHOR = 8
    rowa = strip_row(ANCHOR)
    rows = [strip_row(r) for r in range(16)]
    found = None
    for y in range(ANCHOR, h - 16 + ANCHOR):
        base = (y * w) * 3
        i = img.find(rowa, base, base + w * 3)
        while i >= 0 and not found:
            x = (i - base) // 3
            y0 = y - ANCHOR
            if all(img[((y0+r)*w + x)*3 : ((y0+r)*w + x)*3 + len(rowa)] == rows[r]
                   for r in range(16)):
                found = (x, y0)
            i = img.find(rowa, i + 3, base + w * 3)
        if found: break
    if not found:
        print("  FAIL  the typed command's glyphs are not on screen"); sys.exit(1)
    print(f"  ok    typed text rendered pixel-exactly at {found}")
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  could not check the screendump: {e}"); sys.exit(1)
PYCHECK

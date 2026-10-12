#!/usr/bin/env bash
# Does Doom render the same picture on the framebuffer as it does in mode 13h?
#
# The framebuffer build has no mode 13h: Doom draws into a RAM shadow and
# vga13h_present() scales it up by 3 through the palette. This boots BOTH
# images, screendumps Doom's title screen from each, and compares all 64000
# source pixels.
#
# The comparison carries a tolerance of 4 levels per channel, on purpose.
# QEMU's VGA renders odd 6-bit palette values as (v<<2)|3, replicating the low
# bit, which is an emulator artefact; the framebuffer path does proper bit
# replication so that full scale reaches 255. Matching QEMU exactly would make
# this bit-identical and the colours slightly wrong. A real failure - wrong
# scale, wrong offset, swapped channels, palette not shadowed - lands at 1-45%
# agreement, nowhere near the threshold.
#
# Two blank screens agree too (a cleared console matched 99.8%), so since
# v0.60.253 (review R-T3) each run's serial log must say Doom took the
# screen ("[doom] drawing at", 320x200 in mode 13h, 960x600 on the
# framebuffer), the screendump waits for that line, and the mode 13h title
# must be mostly not black. DOOM_FB_CMD types something else in place of
# doom, to see the test fail without it.
set -uo pipefail
cd "$(dirname "$0")/.."

[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }

WORK=$(mktemp -d /tmp/soupos-doomfb.XXXXXX)
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

make iso-text >/dev/null 2>&1 || { echo "  FAIL  could not build the text ISO"; exit 1; }
make >/dev/null 2>&1          || { echo "  FAIL  could not build the default ISO"; exit 1; }

shoot() {   # shoot <iso> <out.ppm> <serial.log>
    local iso=$1 out=$2 log=$3
    local disk=$WORK/$(basename "$iso").img
    local mon=$WORK/$(basename "$iso").sock
    cp disk.img "$disk"
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom "$iso" -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$log" -monitor "unix:$mon,server,nowait" >/dev/null 2>&1 &
    local pid=$!
    PIDS="$PIDS $pid"
    python3 scripts/qemu_keys.py "$mon" "headchef" "rosemary" "WAIT:1" \
        "${DOOM_FB_CMD:-doom}" "UNTIL:[doom] drawing at" "WAIT:4" >/dev/null 2>&1 || return 1
    python3 - "$mon" "$out" >/dev/null 2>&1 <<PYQ
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.4); s.recv(65536)
s.sendall(("screendump " + sys.argv[2] + "\n").encode()); time.sleep(1.5)
PYQ
    kill "$pid" 2>/dev/null
}

shoot soupOS-text.iso "$WORK/vga.ppm" "$WORK/vga.log" || { echo "  FAIL  mode 13h run: Doom never took the screen"; exit 1; }
shoot soupOS.iso      "$WORK/fb.ppm"  "$WORK/fb.log"  || { echo "  FAIL  framebuffer run: Doom never took the screen"; exit 1; }
grep -aq '\[doom\] drawing at 320x200' "$WORK/vga.log" || { echo "  FAIL  mode 13h: no [doom] drawing at 320x200"; exit 1; }
grep -aq '\[doom\] drawing at 960x600' "$WORK/fb.log"  || { echo "  FAIL  framebuffer: no [doom] drawing at 960x600"; exit 1; }

python3 - "$WORK/vga.ppm" "$WORK/fb.ppm" <<'PYCHECK'
import sys
def ppm(path):
    f = open(path, "rb")
    assert f.readline().strip() == b"P6"
    line = f.readline()
    while line.startswith(b"#"): line = f.readline()
    w, h = map(int, line.split()); f.readline()
    return w, h, f.read()
LIT_MIN = 50                      # the title measured 93.2% (2026-10-12)
try:
    vw, vh, vd = ppm(sys.argv[1])
    fw, fh, fd = ppm(sys.argv[2])
    # QEMU dumps mode 13h doubled, so a source pixel is at (2x, 2y) there and
    # at the centre of its 3x3 block in the scaled framebuffer.
    ox, oy = (fw - 960) // 2, (fh - 600) // 2
    close = tot = 0
    for sy in range(200):
        for sx in range(320):
            o1 = ((sy * 2) * vw + sx * 2) * 3
            o2 = ((oy + sy * 3 + 1) * fw + ox + sx * 3 + 1) * 3
            if max(abs(vd[o1 + c] - fd[o2 + c]) for c in range(3)) <= 4: close += 1
            tot += 1
    pct = 100 * close / tot
    lit = sum(1 for sy in range(200) for sx in range(320) if max(vd[((sy * 2) * vw + sx * 2) * 3 + c] for c in range(3)) > 16)
    lpct = 100 * lit / tot
    print(f"  {close}/{tot} pixels agree within 4 levels ({pct:.2f}%), {lpct:.1f}% of the title not black")
    if lpct < LIT_MIN:
        print(f"  FAIL  the mode 13h screen is {lpct:.1f}% lit: not Doom's title")
        sys.exit(1)
    if pct >= 99.5:
        print("  ok    the framebuffer frame matches the mode 13h frame")
    else:
        print("  FAIL  the scaled frame does not match mode 13h")
        sys.exit(1)
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  comparison failed: {e}")
    sys.exit(1)
PYCHECK

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
set -uo pipefail
cd "$(dirname "$0")/.."

[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }

WORK=$(mktemp -d)
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

make iso-text >/dev/null 2>&1 || { echo "  FAIL  could not build the text ISO"; exit 1; }
make >/dev/null 2>&1          || { echo "  FAIL  could not build the default ISO"; exit 1; }

shoot() {   # shoot <iso> <out.ppm>
    local iso=$1 out=$2
    local disk=$WORK/$(basename "$iso").img
    local mon=$WORK/$(basename "$iso").sock
    cp disk.img "$disk"
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom "$iso" -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -serial null -monitor "unix:$mon,server,nowait" >/dev/null 2>&1 &
    local pid=$!
    PIDS="$PIDS $pid"
    sleep 5
    python3 scripts/qemu_keys.py "$mon" "headchef" "rosemary" "WAIT:1" \
        "doom" "WAIT:10" >/dev/null 2>&1 || return 1
    python3 - "$mon" "$out" >/dev/null 2>&1 <<PYQ
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.4); s.recv(65536)
s.sendall(("screendump " + sys.argv[2] + "\n").encode()); time.sleep(1.5)
PYQ
    kill "$pid" 2>/dev/null
}

shoot soupOS-text.iso "$WORK/vga.ppm" || { echo "  FAIL  mode 13h run"; exit 1; }
shoot soupOS.iso      "$WORK/fb.ppm"  || { echo "  FAIL  framebuffer run"; exit 1; }

python3 - "$WORK/vga.ppm" "$WORK/fb.ppm" <<'PYCHECK'
import sys
def ppm(path):
    f = open(path, "rb")
    assert f.readline().strip() == b"P6"
    line = f.readline()
    while line.startswith(b"#"): line = f.readline()
    w, h = map(int, line.split()); f.readline()
    return w, h, f.read()
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
    print(f"  {close}/{tot} pixels agree within 4 levels ({pct:.2f}%)")
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

#!/usr/bin/env bash
# Is the music the right music, and does it survive sound effects?
#
# Two things, neither of which an ear would settle:
#
#  1. The notes the score says should sound in the first second are actually
#     in the capture. The score is parsed on the host, the MIDI notes turned
#     into frequencies, and each one looked for against a frequency the score
#     does NOT use as a noise floor. A wrong note table, a wrong tick rate or
#     a mis-parsed delay all fail this.
#  2. Music keeps playing while effects play over it. Music is not a mixer
#     voice precisely so it cannot be stolen, and the way to see that is the
#     envelope: it rises when effects start and returns to its own level
#     afterwards, rather than stopping or taking over.
set -uo pipefail
cd "$(dirname "$0")/.."

[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }

LOG=$(mktemp /tmp/soupos-music.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-music.XXXXXX.sock)
WAV=$(mktemp -u /tmp/soupos-music.XXXXXX.wav)
TESTDISK=$(mktemp /tmp/soupos-musicdisk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$WAV" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -audiodev wav,id=a0,path="$WAV" -device AC97,audiodev=a0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5

# The four seconds before the effects are counted from the [music] line,
# when the score has loaded and starts (v0.60.51). Counted from typing
# `hum`, they shrank under load, since loading the lump is slow then, and
# the effects could land inside the music-alone window: two full checks
# failed with "never showed" and the music measured 2466 and 2987, about
# twice its usual 1291.
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "hum D_E1M1" "UNTIL:[music] D_E1M1:" "WAIT:4" "sizzle DSSAWUP DSPISTOL" "WAIT:6" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
sleep 1; kill "$PID" 2>/dev/null; PID=""

grep -E "^\[music\]" "$LOG" | sed 's/^/  /'

python3 - "$WAV" <<'PYCHECK'
import struct, math, sys
try:
    w = open("DOOM1.WAD", "rb").read()
    _, n, off = struct.unpack("<4sII", w[:12])
    fo = None
    for i in range(n):
        o, sz, nm = struct.unpack("<II8s", w[off + i*16: off + i*16 + 16])
        if nm.rstrip(b"\0") == b"D_E1M1": fo = o; break
    if fo is None: print("  skip  D_E1M1 not in the WAD"); sys.exit(0)
    slen, sstart = struct.unpack("<HH", w[fo+4:fo+8])
    score = w[fo+sstart: fo+sstart+slen]

    # Notes started in the first second, by the score's own 140 Hz clock.
    tick = 0.0; i = 0; notes = set()
    while i < len(score) and tick < 1.0:
        b = score[i]; i += 1
        ty = (b >> 4) & 7; ch = b & 0xF
        if ty == 0: i += 1
        elif ty == 1:
            nv = score[i]; i += 1
            if nv & 0x80: i += 1
            if ch != 15: notes.add(nv & 0x7F)
        elif ty in (2, 3): i += 1
        elif ty == 4: i += 2
        elif ty == 6: break
        if b & 0x80:
            d = 0
            while i < len(score):
                v = score[i]; i += 1
                d = (d << 7) | (v & 0x7F)
                if not (v & 0x80): break
            tick += d / 140.0
    freqs = sorted({round(440.0 * 2 ** ((nt - 69) / 12.0)) for nt in notes})[:6]

    d = open(sys.argv[1], "rb").read()
    ch_n = struct.unpack("<H", d[22:24])[0]
    sr = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    s = struct.unpack("<" + "h" * (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])
    left = s[0::ch_n]
    seg = left[:int(0.9 * sr)]

    def power(f):
        re = im = 0.0
        for i2, v in enumerate(seg):
            a = 2 * math.pi * f * i2 / sr
            re += v * math.cos(a); im -= v * math.sin(a)
        return math.hypot(re, im) / len(seg)

    floor = power(1234)          # a frequency the score does not use
    present = sum(1 for f in freqs if power(f) > floor * 4)
    print(f"  {present}/{len(freqs)} of the score's first-second notes are in the capture "
          f"({freqs} Hz, floor {floor:.1f})")
    ok = present == len(freqs)

    # Music alone, then music plus effects, then music alone again. The
    # effects are typed four seconds into the music, but when they start
    # depends on the typing and the load (v0.60.28: fixed windows at 5.0-6.5 s
    # failed full checks twice, the effects starting after the window did).
    # So find their start in the capture. The music alone swings a lot from
    # one quarter second to the next (measured: 430 to 2275), so the start is
    # the first quarter second after the music's own stretch that is half
    # again as loud as the loudest quarter second of that stretch (the
    # effects measured 3800-6200).
    def mean(a, b):
        sl = left[int(a*sr):int(b*sr)]
        return sum(abs(x) for x in sl) / max(1, len(sl))
    total = len(left) / sr
    before = mean(1.0, 3.5)
    loudest = max(mean(1.0 + q / 4, 1.25 + q / 4) for q in range(10))
    onset = None
    t = 3.5
    while t + 0.25 <= total:
        if mean(t, t + 0.25) > loudest * 1.5: onset = t; break
        t += 0.25
    if onset is None:
        print(f"  FAIL  the effects never showed in the capture (music {before:.0f}, {total:.1f} s captured)")
        sys.exit(1)
    during, after = mean(onset, onset + 1.5), mean(onset + 2.5, onset + 4.0)
    nums = f"music {before:.0f}, with effects {during:.0f} from {onset:.2f} s, music again {after:.0f}"
    print(f"  mean amplitude: {nums}")
    if onset + 4.0 > total:
        print(f"  FAIL  the capture ends before the music's return could be measured ({nums}, {total:.1f} s)")
        ok = False
    elif during > before * 1.4 and after > before * 0.4:
        print("  ok    effects played over the music without stopping it")
    else:
        print(f"  FAIL  the music and the effects did not coexist ({nums})")
        ok = False
    sys.exit(0 if ok else 1)
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  could not check the capture: {e}")
    sys.exit(1)
PYCHECK

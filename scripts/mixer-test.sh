#!/usr/bin/env bash
# Do two sounds actually MIX, or does the second one queue behind the first?
#
# Duration alone cannot tell you: a pistol that queues after a saw and a
# pistol that is dropped both leave the saw's length on the clock. So this
# renders the same two lumps on the host, sums them with saturation exactly as
# the kernel does, and correlates envelopes. Mixed output matches the SUM
# better than it matches the louder lump alone; that comparison is the test.
#
# Both lumps start from ONE command, because the keystroke driver takes the
# better part of two seconds per command - long enough that two typed sizzles
# never overlap at all, which is how the first version of this looked like a
# mixing bug when it was a timing artefact.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-mix.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-mix.XXXXXX.sock)
WAV=$(mktemp -u /tmp/soupos-mix.XXXXXX.wav)
TESTDISK=$(mktemp /tmp/soupos-mixdisk.XXXXXX.img)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$WAV" "$TESTDISK"; }
trap cleanup EXIT

[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD, nothing to mix"; exit 0; }
cp disk.img "$TESTDISK"

qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -audiodev wav,id=a0,path="$WAV" -device AC97,audiodev=a0 \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4

python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "sizzle DSSAWUP DSPISTOL" "WAIT:6" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
sleep 1; kill "$PID" 2>/dev/null; PID=""

grep -E "^\[mixer\]|voices at once" "$LOG" | sed 's/^/  /'

python3 - "$WAV" <<'PYCHECK'
import struct, math, sys
try:
    d = open(sys.argv[1], "rb").read()
    ch = struct.unpack("<H", d[22:24])[0]
    sr = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    s = struct.unpack("<" + "h" * (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])
    cap = s[0::ch]
    a = next(i for i, v in enumerate(cap) if abs(v) > 400)
    got = cap[a:]

    w = open("DOOM1.WAD", "rb").read()
    _, n, off = struct.unpack("<4sII", w[:12])
    def lump(name):
        for i in range(n):
            fo, sz, nm = struct.unpack("<II8s", w[off + i*16: off + i*16 + 16])
            if nm.rstrip(b"\0") == name.encode():
                fmt, rate, cnt = struct.unpack("<HHI", w[fo:fo+8])
                return w[fo+8: fo+8+cnt], rate
        raise SystemExit("lump missing")
    def render(name, nframes):
        raw, rate = lump(name); step = rate / sr
        return [((raw[int(i*step)] - 128) << 8) if int(i*step) < len(raw) else 0
                for i in range(nframes)]

    N = len(got)
    saw, pis = render("DSSAWUP", N), render("DSPISTOL", N)
    both = [max(-32768, min(32767, x + y)) for x, y in zip(saw, pis)]
    def env(sig, k=40):
        m = max(1, len(sig) // k)
        return [sum(abs(x) for x in sig[i*m:(i+1)*m]) / m for i in range(k)]
    def corr(x, y):
        num = sum(p*q for p, q in zip(x, y))
        den = math.sqrt(sum(p*p for p in x)) * math.sqrt(sum(q*q for q in y))
        return num / den if den else 0
    e = env(got)
    c_one, c_both = corr(e, env(saw)), corr(e, env(both))
    print(f"  envelope vs the louder lump alone: {c_one:.4f}")
    print(f"  envelope vs the two summed:        {c_both:.4f}")
    if c_both > c_one and c_both > 0.97:
        print(f"  ok    two voices mixed ({len(got)/sr:.3f}s, matches the sum)")
    else:
        print("  FAIL  output does not match the sum: the second voice is missing")
        sys.exit(1)
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  could not check the capture: {e}")
    sys.exit(1)
PYCHECK

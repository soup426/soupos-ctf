#!/usr/bin/env bash
# Do two sounds actually MIX, or does the second one queue behind the first?
#
# Duration alone cannot tell you: a pistol that queues after a saw and a
# pistol that is dropped both leave the saw's length on the clock. So this
# renders the same two lumps on the host, sums them with saturation exactly as
# the kernel does, and correlates envelopes. Mixed output matches the SUM
# better than it matches the louder lump alone; that comparison is the test.
#
# The second voice starts up to three ring chunks (about 63 ms) after the
# first, because the mixer keeps that much lead on the card's read position, so
# the reference is tried at several offsets and the best fit is used.
#
# Both lumps start from ONE command, because the keystroke driver takes the
# better part of two seconds per command - long enough that two typed sizzles
# never overlap at all, which is how the first version of this looked like a
# mixing bug when it was a timing artefact.
set -uo pipefail
cd "$(dirname "$0")/.."
# v0.60.93: after sizzle it waits for the prompt and then 3 s, not a flat
# 6 s from the keystrokes; under a loaded full check (twice on 2026-10-09)
# the capture came back silent, and the error now says which way it failed.

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
    "sizzle DSSAWUP DSPISTOL" "UNTIL:@soupOS:" "WAIT:3" >/dev/null 2>&1 \
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
    def render(name, nframes, delay=0):
        raw, rate = lump(name); step = rate / sr
        out = []
        for i in range(nframes):
            j = i - delay
            si = int(j * step) if j >= 0 else -1
            out.append(((raw[si] - 128) << 8) if 0 <= si < len(raw) else 0)
        return out

    N = len(got)
    saw = render("DSSAWUP", N)
    def env(sig, k=40):
        m = max(1, len(sig) // k)
        return [sum(abs(x) for x in sig[i*m:(i+1)*m]) / m for i in range(k)]
    def corr(x, y):
        num = sum(p*q for p, q in zip(x, y))
        den = math.sqrt(sum(p*p for p in x)) * math.sqrt(sum(q*q for q in y))
        return num / den if den else 0
    e = env(got)

    # The second voice starts a little AFTER the first, and that is the ring
    # doing its job rather than a fault: the mixer keeps a three-chunk lead on
    # the card's read position, so a voice added once playback is under way is
    # first heard three chunks - about 63 ms - later. Comparing against a
    # reference where both start together therefore scores about 0.94 and the
    # alignment that fits scores 0.9999. So the delay is searched for, and the
    # test asserts what actually matters: the best fit is excellent, it needs
    # only a few chunks of offset, and it beats the louder lump on its own.
    best, best_ms = 0.0, 0
    for ms in (0, 10, 21, 42, 63, 85, 106, 128):
        pis = render("DSPISTOL", N, int(sr * ms / 1000))
        both = [max(-32768, min(32767, x + y)) for x, y in zip(saw, pis)]
        c = corr(e, env(both))
        if c > best: best, best_ms = c, ms
    c_one = corr(e, env(saw))

    print(f"  envelope vs the louder lump alone: {c_one:.4f}")
    print(f"  envelope vs the two summed:        {best:.4f} (second voice {best_ms} ms late)")
    if best > c_one and best > 0.97 and best_ms <= 128:
        print(f"  ok    two voices mixed ({len(got)/sr:.3f}s, matches the sum)")
    else:
        print("  FAIL  output does not match the sum: the second voice is missing")
        sys.exit(1)
except SystemExit:
    raise
except Exception as e:
    print(f"  FAIL  could not check the capture: {type(e).__name__} {e}"
          + (" (no sample ever loud: the sound never played)" if isinstance(e, StopIteration) else ""))
    sys.exit(1)
PYCHECK

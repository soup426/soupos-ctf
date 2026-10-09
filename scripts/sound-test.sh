#!/usr/bin/env bash
# The tone and the sound effect, measured from a capture with nothing else in
# it.
#
# These checks used to live in smoke-test.sh and became unreliable there: QEMU's
# wav backend writes only while the card is active, so several sounds played in
# one session land contiguously in the file with no silence to separate them,
# and a span-based measurement picks up whatever played next.
#
# This machine plays a 440 Hz tone, a sound effect, and the same tone again at
# half volume - and because of that contiguity the effect's length is measured
# as the whole span minus the tones. The TONES_MS constant below has to match
# what is actually played; it caught me out once already when the third sound
# was added and a 513 ms effect read as 853.
set -uo pipefail
# v0.60.106: each sound command waits for the prompt and then for its sound,
# not a flat wait from the keystrokes; under a loaded full check (2026-10-09)
# the next command's keys arrived while a sound still played and cut it short.
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-sound.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-sound.XXXXXX.sock)
WAV=$(mktemp -u /tmp/soupos-sound.XXXXXX.wav)
TESTDISK=$(mktemp /tmp/soupos-sounddisk.XXXXXX.img)
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

python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "whistle 440 300" "UNTIL:@soupOS:" "WAIT:2" "sizzle DSPISTOL" "UNTIL:@soupOS:" "WAIT:3" \
    "hush 50" "UNTIL:@soupOS:" "WAIT:1" "whistle 440 300" "UNTIL:@soupOS:" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; exit 1; }
sleep 1; kill "$PID" 2>/dev/null; PID=""

read -r tone_hz sfx_ms <<<"$(python3 - "$WAV" 2>/dev/null <<'PYAUDIO'
import struct, sys
try:
    d = open(sys.argv[1], "rb").read()
    ch = struct.unpack("<H", d[22:24])[0]
    sr = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    s = struct.unpack("<" + "h" * (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])
    left = s[0::ch]
    a = next(i for i, v in enumerate(left) if abs(v) > 400)
    b = next(i for i in range(len(left) - 1, a, -1) if abs(left[i]) > 400)
    t = left[a:a + sr // 4]                  # 250 ms, inside the first tone
    xs = [i for i in range(1, len(t)) if t[i - 1] < 0 <= t[i]]
    hz = round(sr / ((xs[-1] - xs[0]) / (len(xs) - 1))) if len(xs) > 2 else 0
    # QEMU writes only while the card is active, so everything this session
    # plays lands in one contiguous span: the effect's length is the span less
    # the tones around it. TONES_MS must match what the driver below plays -
    # two 300 ms whistles - and adding another sound means changing it.
    TONES_MS = 600
    print(hz, round((b - a) * 1000 / sr) - TONES_MS)
except Exception:
    print(0, 0)
PYAUDIO
)"

fail=0
if [ "${tone_hz:-0}" -ge 438 ] && [ "${tone_hz:-0}" -le 442 ]; then
    echo "  ok    the captured tone measures ${tone_hz} Hz"
else
    echo "  FAIL  the captured tone measures ${tone_hz:-0} Hz, expected 440"; fail=1
fi

# DSPISTOL is 5661 samples at 11025 Hz, so 513 ms of sound, plus the two or
# three 21 ms ring chunks the mixer drains after the voice ends.
if [ "${sfx_ms:-0}" -ge 470 ] && [ "${sfx_ms:-0}" -le 620 ]; then
    echo "  ok    the Doom sound effect lasts ${sfx_ms} ms"
else
    echo "  FAIL  the Doom sound effect lasts ${sfx_ms:-0} ms, expected about 513"; fail=1
fi
# The master level, measured rather than trusted: the same tone played again
# at 50 must peak at half. Scaling happens before the saturation, so turning
# down removes clipping instead of shrinking an already-clipped signal - which
# is why this compares peaks and not just loudness.
peaks=$(python3 - "$WAV" 2>/dev/null <<'PYVOL'
import struct, sys
try:
    d = open(sys.argv[1], "rb").read()
    ch = struct.unpack("<H", d[22:24])[0]
    sr = struct.unpack("<I", d[24:28])[0]
    pcm = d[44:]
    s = struct.unpack("<" + "h" * (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])
    left = s[0::ch]
    # The last 300 ms tone is the quiet one; the first is at full level.
    a = next(i for i, v in enumerate(left) if abs(v) > 400)
    first = max(abs(v) for v in left[a:a + sr // 4])
    b = next(i for i in range(len(left) - 1, a, -1) if abs(left[i]) > 400)
    last = max(abs(v) for v in left[max(a, b - sr // 4):b])
    print(first, last)
except Exception:
    print(0, 0)
PYVOL
)
read -r full half <<<"$peaks"
if [ "${full:-0}" -gt 20000 ] && [ "${half:-0}" -gt 0 ]    && [ $(( full / (half > 0 ? half : 1) )) -eq 2 ]; then
    echo "  ok    hush 50 halved the peak ($full -> $half)"
else
    echo "  FAIL  hush 50 gave $full -> $half, expected about half"; fail=1
fi

exit $fail

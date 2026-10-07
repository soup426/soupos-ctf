#!/usr/bin/env bash
# The tone and the sound effect, measured from a capture with nothing else in
# it.
#
# These checks used to live in smoke-test.sh and became unreliable there: QEMU's
# wav backend writes only while the card is active, so several sounds played in
# one session land contiguously in the file with no silence to separate them,
# and a span-based measurement picks up whatever played next. This boots a
# machine that plays exactly two things.
set -uo pipefail
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
    "whistle 440 300" "WAIT:4" "sizzle DSPISTOL" "WAIT:5" >/dev/null 2>&1 \
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
    t = left[a:a + sr // 4]                  # 250 ms, inside the tone
    xs = [i for i in range(1, len(t)) if t[i - 1] < 0 <= t[i]]
    hz = round(sr / ((xs[-1] - xs[0]) / (len(xs) - 1))) if len(xs) > 2 else 0
    print(hz, round((b - a) * 1000 / sr) - 300)
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
exit $fail

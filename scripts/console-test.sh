#!/usr/bin/env bash
# soupOS serial console test.
#
# Boots with COM1 on a TCP socket — exactly how a hosted instance runs — and
# drives it the way a player's `nc` session would: no VGA window, no PS/2
# keyboard, nothing but a byte stream. Verifies both halves of the console:
# output mirroring (does the player see the shell?) and input translation
# (do CR, DEL and ANSI arrows reach the line editor as the right codes?).
#
#   ./scripts/console-test.sh
#   KEEP=1 ./scripts/console-test.sh    # keep the session transcript
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=${PORT:-4457}
LOG=$(mktemp /tmp/soupos-console.XXXXXX.log)
QEMU_PID=""

cleanup() {
    [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null
    if [ "${KEEP:-0}" = "1" ] || [ "${fail:-0}" != "0" ]; then echo "transcript kept at $LOG"; else rm -f "$LOG"; fi
    rm -f "${TESTDISK:-}"
}
trap cleanup EXIT

[ -f soupOS.iso ] || { echo "soupOS.iso missing — run 'make' first"; exit 1; }
[ -f disk.img ]   || { echo "disk.img missing — run 'make disk' first"; exit 1; }

# A copy, like every other script. Until v0.41.0 this one booted disk.img
# itself, so every check run logged in on the canonical image and left
# /etc/kitchen behind - which is how fsck.fat found an old-layout entry on a
# "pristine" disk. check.sh now fails if disk.img changes during a run.
TESTDISK=$(mktemp /tmp/soupos-consoledisk.XXXXXX.img)
cp disk.img "$TESTDISK"
echo "booting soupOS with COM1 on tcp/$PORT (no display, no keyboard)..."
qemu-system-i386 \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown \
    -display none \
    -serial "tcp:127.0.0.1:$PORT,server" >/dev/null 2>&1 &
QEMU_PID=$!
sleep 1

python3 - "$PORT" "$LOG" <<'PYEOF' || { echo "FAIL: could not drive the console"; exit 1; }
import socket, sys, time
port, logpath = int(sys.argv[1]), sys.argv[2]

s = None
for _ in range(60):
    try:
        s = socket.create_connection(('127.0.0.1', port), timeout=20); break
    except OSError:
        time.sleep(0.5)
if s is None:
    sys.exit('no connection')
s.settimeout(2.0)

chunks = []
def drain(t=2.0):
    end = time.time() + t
    while time.time() < end:
        try:
            b = s.recv(65536)
            if not b: break
            chunks.append(b)
        except socket.timeout:
            break

def send(raw, wait=1.5):
    s.sendall(raw)
    time.sleep(0.2)
    drain(wait)

# Wait for the login prompt itself. This was drain(8), which stops at the
# first 2 s of silence: under the full check's load the boot (no KVM here)
# went quiet that long before the kernel was up, the test typed into the
# bootloader, and the first byte was lost ("cook: eadchef"), twice
# (2026-10-08). Measured then: a byte sent once the kernel runs is kept and
# reaches the login; only one sent before the kernel exists is lost.
import re
end = time.time() + 120
while time.time() < end and b"clock in to start your shift" not in b"".join(chunks):
    drain(1.0)
time.sleep(0.5)
send(b"headchef\r", 1.5)                  # CR is what a terminal sends
send(b"rosemary\r", 2.0)
send(b"whoami\r", 1.5)
# Line editing over the wire: type a typo, erase it with DEL (0x7f), finish.
send(b"roste", 0.5)
send(b"XX", 0.5)
send(b"\x7f\x7f", 0.5)                    # terminal backspace
send(b"r\r", 2.0)
# ANSI arrows must not leak raw escape bytes into the command line.
send(b"pwd\x1b[D\x1b[C\r", 2.0)
# The last command has no later command whose reads would catch slow
# output, so wait for its answer itself (up to 10 s).
end = time.time() + 10
while time.time() < end and not re.search(rb"(?m)^\s*/\s*$", b"".join(chunks)):
    drain(1.0)

open(logpath, 'wb').write(b''.join(chunks))
s.close()
PYEOF

fail=0
check() {
    if grep -qaE "$2" "$LOG"; then echo "  ok    $1"
    else echo "  FAIL  $1  (no match for /$2/)"; fail=1; fi
}

echo "checking the session transcript:"
check "boot output reaches the socket"    '\[boot\] kernel_main'
check "banner mirrored (VGA -> serial)"   'a shitty kernel'
check "login prompt reached the player"   'clock in to start your shift'
check "CR accepted as Enter (login ok)"   'Welcome to the kitchen'
check "shell prompt rendered"             'headchef@soupOS'
check "command executed over serial"      'uid 0'
check "DEL erased the typo -> 'roster'"   'Kitchen roster'
check "arrows not leaked as raw bytes"    '^\s*/\s*$'

if grep -qa 'KERNEL PANIC' "$LOG"; then
    echo "  FAIL  kernel panicked"; fail=1
else
    echo "  ok    no kernel panic"
fi

echo
if [ "$fail" = 0 ]; then echo "CONSOLE TEST PASSED"; else echo "CONSOLE TEST FAILED"; fi
exit $fail

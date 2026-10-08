#!/usr/bin/env bash
# motd-test.sh - a message of the day (v0.55.6).
#
# /etc/motd is shown after every interactive login - console and vault (the
# pass is the console) - but not to `ssh host command`, whose output a
# script may be reading. Control characters are dropped, so the file cannot
# send a terminal escape sequences. The file is pinned to the headchef.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18240
fail=0
WORK=$(mktemp -d /tmp/soupos-motd.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'Inspection at noon.\nWash your hands.\x1b[2J\n' > "$WORK/motd"
mmd -i "$WORK/disk.img" ::/etc 2>/dev/null
mcopy -o -i "$WORK/disk.img" "$WORK/motd" ::/etc/motd
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "vault 22" "WAIT:2" "perms /etc/motd" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
python3 - "$PORT" <<'PY' || fail=1
import pexpect, subprocess, sys
port = sys.argv[1]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10"
fail = 0
def say(ok, text):
    global fail
    print(("  ok    " if ok else "  FAIL  ") + text); fail |= (not ok)
c = pexpect.spawn(f"ssh -tt -p {port} {o} cook@127.0.0.1", encoding="utf-8", timeout=30)
c.expect("password:"); c.sendline("soup"); c.expect("Welcome to the kitchen")
i = c.expect([r"Inspection at noon\.\s+Wash your hands\.", pexpect.TIMEOUT], timeout=10)
c.expect("> "); before = c.before
say(i == 0 and "\x1b[2J" not in before, "an SSH login shows the message, its escape sequence dropped")
c.sendline("clockout"); c.close(force=True)
c = pexpect.spawn(f"ssh -p {port} {o} cook@127.0.0.1 whoami", encoding="utf-8", timeout=30)
c.expect("password:"); c.sendline("soup"); c.expect(pexpect.EOF); out = c.before
say("cook  (uid 1)" in out and "Inspection" not in out, "ssh host command gets its answer and no message")
sys.exit(fail)
PY
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
# The ESC byte is dropped; what followed it prints as plain text.
echo "$L" | grep "^  Inspection at noon\.$" >/dev/null && echo "$L" | grep "^  Wash your hands\.\[2J$" >/dev/null && ! grep -q $'\x1b\[2J' "$WORK/serial.log" \
    && echo "  ok    a console login shows the message, line by line" || { echo "  FAIL  console motd"; fail=1; }
echo "$L" | grep "owner: headchef   perms: rw-r--" >/dev/null \
    && echo "  ok    /etc/motd is pinned to the headchef, rw-r--" || { echo "  FAIL  /etc/motd's owner or mode"; fail=1; }
exit $fail

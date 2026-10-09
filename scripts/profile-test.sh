#!/usr/bin/env bash
# profile-test.sh - ~/profile runs at interactive logins (v0.60.5).
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18260
fail=0
WORK=$(mktemp -d /tmp/soupos-profile.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "vault 22" "WAIT:2" "clockout" "WAIT:1" \
    "cook" "soup" "WAIT:2" "stir ~/profile \"X=7 ; cook call.elf ran >> ~/P1.TXT ; nosuchthing\"" "WAIT:1" "clockout" "WAIT:1" \
    "cook" "soup" "WAIT:3" "cook call.elf x=\$X > ~/P2.TXT" "WAIT:2" "whoami" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
python3 - "$PORT" <<'PY' || fail=1
import pexpect, sys
port = sys.argv[1]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no"
c = pexpect.spawn(f"ssh -tt -p {port} {o} cook@127.0.0.1", encoding="utf-8", timeout=30)
c.expect("password:"); c.sendline("soup"); c.expect("Welcome"); c.expect("> ")
c.sendline("clockout"); c.close(force=True)
c = pexpect.spawn(f"ssh -p {port} {o} cook@127.0.0.1 whoami", encoding="utf-8", timeout=30)
c.expect("password:"); c.sendline("soup"); c.expect(pexpect.EOF); c.close()
PY
sleep 1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
[ "$(val /home/cook/P1.TXT | grep -c ran)" = "2" ] \
    && echo "  ok    the profile ran at the console login and the interactive SSH login, not for ssh host cmd" \
    || { echo "  FAIL  the profile ran $(val /home/cook/P1.TXT | grep -c ran) times, wanted 2"; fail=1; }
[ "$(val /home/cook/P2.TXT)" = "x=7" ] && echo "  ok    a variable the profile set is there for the session" || { echo "  FAIL  \$X after login: [$(val /home/cook/P2.TXT)]"; fail=1; }
tr -d '\r' < "$WORK/serial.log" | grep "No soup for you: 'nosuchthing'" >/dev/null && tr -d '\r' < "$WORK/serial.log" | grep -E "cook  \(uid 1\)" >/dev/null \
    && echo "  ok    an error in the profile is said, and the login goes on" || { echo "  FAIL  the profile's error"; fail=1; }
exit $fail

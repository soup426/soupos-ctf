#!/usr/bin/env bash
# kbdtap-test.sh - /dev/kbd is the console's keyboard, for the console only
# (v0.60.264, review R-K13). v0.60.257 sent a program's reads through the
# stdin rule, but ring-0 readers still reached dev_kbd_read with nothing
# asked: soupyc's open("/dev/kbd") (and a spawn()ed soupyc task outlives a
# shift), and a shell command in an SSH session (pour /dev/kbd there read
# the console's keys, not its own). soupyc's open of it gives -1 now, and
# the SSH session's pour ends at once with nothing.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18280
WORK=$(mktemp -d /tmp/soupos-kbdtap.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'vault 22\n' > "$WORK/rc"
mmd -i "$WORK/disk.img" ::/etc 2>/dev/null
mcopy -i "$WORK/disk.img" "$WORK/rc" ::/etc/rc
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    'soup -c let k = open("/dev/kbd") pour "KBD " + k' "UNTIL:KBD " "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
tr -d '\r' < "$WORK/log" | grep '^KBD -1' >/dev/null && echo "  ok    soupyc's open(\"/dev/kbd\") is refused" \
    || { echo "  FAIL  soupyc opened /dev/kbd: $(tr -d '\r' < "$WORK/log" | grep -o '^KBD .*' | head -1)"; fail=1; }
python3 - "$PORT" <<'PY' && echo "  ok    pour /dev/kbd in an SSH session ends at once, reading nothing of the console's" \
    || { echo "  FAIL  pour /dev/kbd in an SSH session waited on the console's keyboard"; fail=1; }
import pexpect, sys, time
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1"
c = pexpect.spawn(f"ssh -p {sys.argv[1]} {o} headchef@127.0.0.1 'pour /dev/kbd'", encoding="utf-8", timeout=25)
c.expect("password:"); c.sendline("rosemary")
t0 = time.time()
try:
    c.expect(pexpect.EOF, timeout=10)
except pexpect.TIMEOUT:
    sys.exit(1)
sys.exit(0 if time.time() - t0 < 10 else 1)
PY
[ "$fail" = 0 ] && echo "kbdtap-test: PASS" || echo "kbdtap-test: FAIL"
exit "$fail"

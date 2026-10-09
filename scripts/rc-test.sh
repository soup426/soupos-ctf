#!/usr/bin/env bash
# Does a machine come up ready, with nothing typed at its console?
#
# A copy of the disk gets /etc/rc reading "vault 22". soupOS is booted with
# port 22 forwarded and NOT ONE KEY is sent to its console: the kernel leases
# an address by itself, /etc/rc opens the vault before the login prompt, and
# an `ssh host whoami` from outside must then work.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18130
TESTDISK=$(mktemp /tmp/soupos-rcdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-rc.XXXXXX.log)
RC=$(mktemp /tmp/soupos-rc.XXXXXX.txt)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$LOG" "$TESTDISK" "$RC"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
printf '# opened at boot, before anyone logs in\nvault 22\n' > "$RC"
mmd -i "$TESTDISK" ::/etc 2>/dev/null
mcopy -i "$TESTDISK" "$RC" ::/etc/rc
qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$LOG" >/dev/null 2>&1 &
PID=$!
# Nothing is typed, ever. Wait for the login prompt, which comes after
# /etc/rc has run, rather than a fixed 8 s: under the full check's load
# that once was not long enough (2026-10-08), the same blind wait that
# qemu_keys stopped making.
for i in $(seq 1 120); do grep -q "clock in to start your shift" "$LOG" 2>/dev/null && break; sleep 0.5; done
sleep 1

fail=0
grep -q "IP 10.0.2.15 (dhcp)" "$LOG" && echo "  ok    the kernel leased an address by itself" \
    || { echo "  FAIL  no DHCP lease at boot"; fail=1; }
grep -q "^\[rc\] vault 22" "$LOG" && echo "  ok    /etc/rc ran at boot, skipping its comment" \
    || { echo "  FAIL  /etc/rc did not run"; fail=1; }
grep -q "^\[rc\] #" "$LOG" && { echo "  FAIL  a comment line was run"; fail=1; }
python3 - "$PORT" <<'PY' && echo "  ok    ssh works with nothing typed at the console" \
    || { echo "  FAIL  ssh after an rc-opened vault"; fail=1; }
import pexpect, sys
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o NumberOfPasswordPrompts=1"
c = pexpect.spawn(f"ssh -p {sys.argv[1]} {o} headchef@127.0.0.1 whoami", encoding="utf-8", timeout=25)
c.expect("password:"); c.sendline("rosemary")
c.expect(r"headchef\s+\(uid 0\)"); c.expect(pexpect.EOF); c.close()
sys.exit(0 if c.exitstatus == 0 else 1)
PY
grep -q "clock in to start your shift" "$LOG" && echo "  ok    the console still reached its login prompt" \
    || { echo "  FAIL  the console never reached the login prompt"; fail=1; }
exit $fail

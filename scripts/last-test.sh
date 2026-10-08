#!/usr/bin/env bash
# last-test.sh - the login record survives a reboot (v0.53.2).
#
# First boot: a refused console login, an accepted one, then over SSH one
# wrong password and one right one. Second boot of the same disk: `last`
# must show all four, newest first; mtools must read /etc/logins; the file
# must be the headchef's however many cooks wrote it; fsck must be clean.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18170
fail=0
WORK=$(mktemp -d /tmp/soupos-last.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# A full record already on the disk (64 lines, the cap), so the logins below
# must push the oldest out. Line 1 is the one that must go first.
for i in $(seq 1 64); do printf '2020-01-01 00:00  ok       seed%-13s console\n' "$i"; done > "$WORK/seed"
mmd -i "$WORK/disk.img" ::/etc 2>/dev/null
mcopy -o -i "$WORK/disk.img" "$WORK/seed" ::/etc/logins

start() {       # start <log>: boot the shared disk with port 22 forwarded
    rm -f "$WORK/mon.sock"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
        -serial "file:$1" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!; sleep 5
}
keys() { python3 scripts/qemu_keys.py "$WORK/mon.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }; }
stop() { kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""; }

# ── first boot ──
start "$WORK/one.log"
keys "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
     "saucier" "wrongbasil" "WAIT:2" "saucier" "basil" "WAIT:2" "clockout" "WAIT:1" \
     "headchef" "rosemary" "WAIT:2" "vault 22" "WAIT:2"
python3 - "$PORT" <<'PY' || fail=1
import pexpect, sys
port = sys.argv[1]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10"
c = pexpect.spawn(f"ssh -p {port} {o} -o NumberOfPasswordPrompts=2 saucier@127.0.0.1 whoami", encoding="utf-8", timeout=40)
c.expect("password:"); c.sendline("nottheone")
c.expect("password:"); c.sendline("basil")
i = c.expect([r"saucier\s+\(uid", pexpect.EOF, pexpect.TIMEOUT])
c.close(force=True)
print("  ok    an SSH login after one wrong password" if i == 0 else "  FAIL  the SSH login did not succeed")
sys.exit(0 if i == 0 else 1)
PY
sleep 1
stop

# ── second boot, the same disk ──
start "$WORK/two.log"
keys "headchef" "rosemary" "WAIT:2" "last" "WAIT:2"
stop
L=$(tr -d '\r' < "$WORK/two.log" | grep -E '^  [0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}  (ok|refused) ' )
want='ok +headchef +console
ok +saucier +10\.0\.2\.2
refused +saucier +10\.0\.2\.2
ok +headchef +console
ok +saucier +console
refused +saucier +console
ok +headchef +console'
got=$(echo "$L" | sed -E 's/^  [0-9-]+ [0-9:]+  //' | head -7)
if echo "$got" | paste -d'|' - <(echo "$want") | awk -F'|' '{ if ($1 !~ "^"$2) bad=1 } END { exit bad }'; then
    echo "  ok    last shows the first boot's logins after a reboot, newest first, refusals included"
else
    echo "  FAIL  last after the reboot:"; echo "$L" | head -8 | sed 's/^/        /'; fail=1
fi
mtype -i "$WORK/disk.img" ::/etc/logins 2>/dev/null | grep -qE "refused +saucier +10\.0\.2\.2" \
    && echo "  ok    mtools reads /etc/logins" || { echo "  FAIL  mtools could not read the record"; fail=1; }
grep -q "owner: headchef" <(start "$WORK/three.log"; keys "headchef" "rosemary" "WAIT:2" "perms /etc/logins" "WAIT:1"; stop; tr -d '\r' < "$WORK/three.log") \
    && echo "  ok    the record stays the headchef's though saucier's logins wrote it" || { echo "  FAIL  /etc/logins owner"; fail=1; }
n=$(mtype -i "$WORK/disk.img" ::/etc/logins 2>/dev/null | grep -c .)
if [ "$n" = "64" ] && ! mtype -i "$WORK/disk.img" ::/etc/logins | grep -qE "seed1 " && mtype -i "$WORK/disk.img" ::/etc/logins | grep -qE "seed64 "; then
    echo "  ok    the record keeps the newest 64 lines; the oldest seeded ones are gone"
else
    echo "  FAIL  trimming: $n lines"; fail=1
fi
[ "$(fsck.fat -n "$WORK/disk.img" 2>&1 | wc -l)" -le 2 ] \
    && echo "  ok    fsck is clean" || { echo "  FAIL  fsck"; fsck.fat -n "$WORK/disk.img"; fail=1; }
exit $fail

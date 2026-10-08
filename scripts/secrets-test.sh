#!/usr/bin/env bash
# secrets-test.sh - the files that hold the kitchen's secrets (v0.54.1).
#
# fire removes the fired cook's SSH keys: /AUTHKEYS names a cook, not a
# person, so before this a cook later hired under the same name logged in
# with the fired one's key. Other cooks' lines and bare (headchef) lines
# stay.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18190
fail=0
WORK=$(mktemp -d /tmp/soupos-secrets.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT

cp disk.img "$WORK/disk.img"
ssh-keygen -q -t ed25519 -N "" -C saucier-key -f "$WORK/sk"
ssh-keygen -q -t ed25519 -N "" -C cook-key    -f "$WORK/ck"
ssh-keygen -q -t ed25519 -N "" -C chef-key    -f "$WORK/hk"
{ echo "saucier $(cat "$WORK/sk.pub")"; echo "cook $(cat "$WORK/ck.pub")"; cat "$WORK/hk.pub"; } > "$WORK/AUTHKEYS"
mcopy -o -i "$WORK/disk.img" "$WORK/AUTHKEYS" ::/AUTHKEYS

qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "fire saucier" "WAIT:3" \
    "hire saucier" "WAIT:1" "thyme" "WAIT:1" "thyme" "WAIT:2" "vault 22" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }

O="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o IdentitiesOnly=yes -o PasswordAuthentication=no -o KbdInteractiveAuthentication=no -o ConnectTimeout=10 -p $PORT"
try() { timeout 30 ssh $O -i "$1" "$2@127.0.0.1" whoami 2>&1 | tr -d '\r'; }
out=$(try "$WORK/sk" saucier)
echo "$out" | grep "Permission denied" >/dev/null && ! echo "$out" | grep "saucier  (uid" >/dev/null \
    && echo "  ok    the fired saucier's key does not open the new saucier's account" \
    || { echo "  FAIL  the fired cook's key logged in as the new saucier"; echo "$out" | head -3 | sed 's/^/        /'; fail=1; }
try "$WORK/ck" cook | grep "cook  (uid 1)" >/dev/null \
    && echo "  ok    another cook's key still works" || { echo "  FAIL  cook's key stopped working"; fail=1; }
try "$WORK/hk" headchef | grep "headchef  (uid 0)" >/dev/null \
    && echo "  ok    a bare (headchef) line still works" || { echo "  FAIL  the headchef's bare key stopped working"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep "1 SSH key for saucier removed from /AUTHKEYS" >/dev/null \
    && echo "  ok    fire says how many keys it removed" || { echo "  FAIL  fire did not report the key"; fail=1; }
left=$(mtype -i "$WORK/disk.img" ::/AUTHKEYS 2>/dev/null)
! echo "$left" | grep "^saucier " >/dev/null && [ "$(echo "$left" | grep -c ssh-ed25519)" = "2" ] \
    && echo "  ok    /AUTHKEYS keeps the other two lines and loses saucier's (mtools)" || { echo "  FAIL  /AUTHKEYS after fire:"; echo "$left" | sed 's/^/        /'; fail=1; }
# ── every system file has a known owner and mode (v0.54.2) ──
# Proven on v0.54.1: /HOSTKEY.ED, the vault's private host key, was rwxr-x
# and an ordinary cook read it; the public key derived from those 32 bytes
# was the one the vault served. And /etc/rc, run as the headchef at boot,
# was run whatever its owner and mode.
two() {         # two <disk> <log> <keys...>: one boot, no network forward
    local disk=$1 log=$2; shift 2
    rm -f "$WORK/m2.sock"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -netdev user,id=n0 -device rtl8139,netdev=n0 \
        -serial "file:$log" -monitor "unix:$WORK/m2.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/m2.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
cp disk.img "$WORK/sys.img"
printf '# opens the vault, which makes the host key\nvault 22\n' > "$WORK/rc"
mmd -i "$WORK/sys.img" ::/etc 2>/dev/null
mcopy -o -i "$WORK/sys.img" "$WORK/rc" ::/etc/rc
mcopy -o -i "$WORK/sys.img" "$WORK/AUTHKEYS" ::/AUTHKEYS
two "$WORK/sys.img" "$WORK/sys1.log" "headchef" "rosemary" "WAIT:2" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pour /HOSTKEY.ED" "WAIT:1" "cook spoon.elf /HOSTKEY.ED" "WAIT:2" \
    "stir /etc/rc vault off" "WAIT:1" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "perms /etc/rc rwxrwx" "WAIT:1"
two "$WORK/sys.img" "$WORK/sys2.log" "headchef" "rosemary" "WAIT:2" \
    "perms /etc" "WAIT:1" "perms /home" "WAIT:1" "perms /etc/logins" "WAIT:1" "perms /AUTHKEYS" "WAIT:1" \
    "perms /HOSTKEY.ED" "WAIT:1" "perms /etc/kitchen" "WAIT:1"
tr -d '\r' < "$WORK/sys1.log" > "$WORK/s1"; tr -d '\r' < "$WORK/sys2.log" > "$WORK/s2"
grep -q "^\[rc\] vault 22" "$WORK/s1" && grep -q "Permission denied: /HOSTKEY.ED" "$WORK/s1" \
    && grep -q "/spoon.elf: open /HOSTKEY.ED for reading refused" "$WORK/s1" \
    && echo "  ok    a cook cannot read the vault's private host key, from the shell or a program" \
    || { echo "  FAIL  the host key was readable (or the vault never opened)"; fail=1; }
grep -q "Permission denied: /etc" "$WORK/s1" \
    && echo "  ok    a cook cannot write /etc/rc" || { echo "  FAIL  saucier wrote /etc/rc"; fail=1; }
grep -q "^\[rc\] /etc/rc not run: owned by uid 0" "$WORK/s2" && ! grep -q "^\[rc\] vault 22" "$WORK/s2" \
    && echo "  ok    an /etc/rc other cooks can write is not run at boot" || { echo "  FAIL  an unsafe /etc/rc ran"; fail=1; }
want='/etc rwxr-x
/home rwxr-x
/etc/logins rw-r--
/AUTHKEYS rw-r--
/HOSTKEY.ED rw----
/etc/kitchen rw----'
bad=0
while read -r path mode; do
    awk -v p="  $path" 'f && /owner:/ {print; exit} $0 == p {f=1}' "$WORK/s2" | grep "owner: headchef   perms: $mode" >/dev/null || { echo "        $path is not headchef $mode"; bad=1; }
done <<< "$want"
[ "$bad" = 0 ] && echo "  ok    every system file is the headchef's with its pinned mode, after a session of use" \
    || { echo "  FAIL  system file owners and modes"; fail=1; }
exit $fail

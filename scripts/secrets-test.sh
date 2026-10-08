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
echo "$out" | grep -q "Permission denied" && ! echo "$out" | grep -q "saucier  (uid" \
    && echo "  ok    the fired saucier's key does not open the new saucier's account" \
    || { echo "  FAIL  the fired cook's key logged in as the new saucier"; echo "$out" | head -3 | sed 's/^/        /'; fail=1; }
try "$WORK/ck" cook | grep -q "cook  (uid 1)" \
    && echo "  ok    another cook's key still works" || { echo "  FAIL  cook's key stopped working"; fail=1; }
try "$WORK/hk" headchef | grep -q "headchef  (uid 0)" \
    && echo "  ok    a bare (headchef) line still works" || { echo "  FAIL  the headchef's bare key stopped working"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -q "1 SSH key for saucier removed from /AUTHKEYS" \
    && echo "  ok    fire says how many keys it removed" || { echo "  FAIL  fire did not report the key"; fail=1; }
left=$(mtype -i "$WORK/disk.img" ::/AUTHKEYS 2>/dev/null)
! echo "$left" | grep -q "^saucier " && [ "$(echo "$left" | grep -c ssh-ed25519)" = "2" ] \
    && echo "  ok    /AUTHKEYS keeps the other two lines and loses saucier's (mtools)" || { echo "  FAIL  /AUTHKEYS after fire:"; echo "$left" | sed 's/^/        /'; fail=1; }
exit $fail

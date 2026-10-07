#!/usr/bin/env bash
# Does a real ssh client get through the transport?
#
# Boots with port 22 forwarded, opens the vault, and runs OpenSSH against it
# with every auth method off, so the expected ending is "Permission denied
# (password)": that line is only reached after the key exchange succeeded,
# NEWKEYS went both ways, and SERVICE_ACCEPT came back ENCRYPTED. The
# fingerprint the client prints must be the one the kernel logged, and after
# a reboot of the same disk it must not change.
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=18023
TESTDISK=$(mktemp /tmp/soupos-sshdisk.XXXXXX.img)
LOG=""; MON=""; PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT
cp disk.img "$TESTDISK"

boot() {
    LOG=$(mktemp /tmp/soupos-ssh.XXXXXX.log)
    MON=$(mktemp -u /tmp/soupos-ssh.XXXXXX.sock)
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
        -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
    PID=$!
    sleep 5
    python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
        "plumbing dhcp" "WAIT:6" "vault 22" "WAIT:2" >/dev/null 2>&1
}
halt() { kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""; sleep 1; }

boot
client=$(timeout 40 ssh -vvv -p "$PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PasswordAuthentication=no -o KbdInteractiveAuthentication=no -o PubkeyAuthentication=no \
    -o ConnectTimeout=10 headchef@127.0.0.1 2>&1)
kfp1=$(grep -oE "^\[ssh\] host key SHA256:[A-Za-z0-9+/]+" "$LOG" | awk '{print $4}')
cfp=$(echo "$client" | grep -oE "Server host key: ssh-ed25519 SHA256:[A-Za-z0-9+/]+" | awk '{print $5}')
fail=0
echo "$client" | grep -q "kex: algorithm: curve25519-sha256" \
    && echo "  ok    curve25519-sha256 negotiated" || { echo "  FAIL  kex not negotiated"; fail=1; }
echo "$client" | grep -q "cipher: chacha20-poly1305@openssh.com" \
    && echo "  ok    chacha20-poly1305 negotiated" || { echo "  FAIL  cipher not negotiated"; fail=1; }
echo "$client" | grep -q "SSH2_MSG_NEWKEYS received" \
    && echo "  ok    NEWKEYS both ways" || { echo "  FAIL  no NEWKEYS from the server"; fail=1; }
echo "$client" | grep -q "SSH2_MSG_SERVICE_ACCEPT received" \
    && echo "  ok    the first encrypted packet decrypted on the client" || { echo "  FAIL  no SERVICE_ACCEPT"; fail=1; }
echo "$client" | grep -q "Permission denied (password)" \
    && echo "  ok    refused with password offered" || { echo "  FAIL  did not end at Permission denied"; fail=1; }
[ -n "$cfp" ] && [ "$cfp" = "$kfp1" ] \
    && echo "  ok    client saw the kernel's fingerprint ($cfp)" || { echo "  FAIL  fingerprint mismatch ('$cfp' vs '$kfp1')"; fail=1; }
halt; rm -f "$LOG" "$MON"

boot
kfp2=$(grep -oE "^\[ssh\] host key SHA256:[A-Za-z0-9+/]+" "$LOG" | awk '{print $4}')
grep -q "new host key written" "$LOG" \
    && { echo "  FAIL  host key regenerated on reboot"; fail=1; } \
    || echo "  ok    host key loaded from disk on reboot"
[ -n "$kfp1" ] && [ "$kfp1" = "$kfp2" ] \
    && echo "  ok    fingerprint unchanged across reboot" || { echo "  FAIL  fingerprint changed ('$kfp1' -> '$kfp2')"; fail=1; }
exit $fail

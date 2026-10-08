#!/usr/bin/env bash
# Does a real ssh client get through the transport?
#
# Boots with port 22 forwarded, opens the vault, and runs OpenSSH against it
# with every auth method off, so the expected ending is "Permission denied
# (password)": that line is only reached after the key exchange succeeded,
# NEWKEYS went both ways, and SERVICE_ACCEPT came back ENCRYPTED. The
# fingerprint the client prints must be the one the kernel logged, and after
# a reboot of the same disk it must not change. Then the real thing, via
# scripts/ssh-login.py: a password login, two commands answered through the
# channel, and a wrong password refused.
set -uo pipefail
cd "$(dirname "$0")/.."

PORT=18023
TESTDISK=$(mktemp /tmp/soupos-sshdisk.XXXXXX.img)
LOG=""; MON=""; PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; rm -rf "${KEYDIR:-}"; }
trap cleanup EXIT
cp disk.img "$TESTDISK"
# A client key, authorised by putting its public line on the disk as
# /AUTHKEYS before the first boot. soupOS never sees the private half.
KEYDIR=$(mktemp -d /tmp/soupos-sshkey.XXXXXX)
ssh-keygen -q -t ed25519 -N '' -f "$KEYDIR/id" >/dev/null 2>&1
# A second key, for saucier, in the v0.49.0 form: the cook's name in front.
# The first stays a bare .pub line, which reads as the headchef's only.
ssh-keygen -q -t ed25519 -N '' -f "$KEYDIR/sid" >/dev/null 2>&1
{ cat "$KEYDIR/id.pub"; printf 'saucier '; cat "$KEYDIR/sid.pub"; } > "$KEYDIR/authkeys"
mcopy -i "$TESTDISK" "$KEYDIR/authkeys" ::AUTHKEYS

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
        "plumbing dhcp" "WAIT:6" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
        "vault rekey 8" "vault 22" "WAIT:2" >/dev/null 2>&1
}
halt() { kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""; sleep 1; }

boot
client=$(timeout 40 ssh -vvv -p "$PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PasswordAuthentication=no -o KbdInteractiveAuthentication=no -o PubkeyAuthentication=no \
    -o ConnectTimeout=10 headchef@127.0.0.1 2>&1)
kfp1=$(grep -oE "^\[ssh\] host key SHA256:[A-Za-z0-9+/]+" "$LOG" | awk '{print $4}')
cfp=$(echo "$client" | grep -oE "Server host key: ssh-ed25519 SHA256:[A-Za-z0-9+/]+" | awk '{print $5}')
fail=0
echo "$client" | grep "kex: algorithm: curve25519-sha256" >/dev/null \
    && echo "  ok    curve25519-sha256 negotiated" || { echo "  FAIL  kex not negotiated"; fail=1; }
echo "$client" | grep "cipher: chacha20-poly1305@openssh.com" >/dev/null \
    && echo "  ok    chacha20-poly1305 negotiated" || { echo "  FAIL  cipher not negotiated"; fail=1; }
echo "$client" | grep "SSH2_MSG_NEWKEYS received" >/dev/null \
    && echo "  ok    NEWKEYS both ways" || { echo "  FAIL  no NEWKEYS from the server"; fail=1; }
echo "$client" | grep "SSH2_MSG_SERVICE_ACCEPT received" >/dev/null \
    && echo "  ok    the first encrypted packet decrypted on the client" || { echo "  FAIL  no SERVICE_ACCEPT"; fail=1; }
echo "$client" | grep "Permission denied (publickey,password)" >/dev/null \
    && echo "  ok    refused with publickey and password offered" || { echo "  FAIL  did not end at Permission denied"; fail=1; }
[ -n "$cfp" ] && [ "$cfp" = "$kfp1" ] \
    && echo "  ok    client saw the kernel's fingerprint ($cfp)" || { echo "  FAIL  fingerprint mismatch ('$cfp' vs '$kfp1')"; fail=1; }
python3 scripts/ssh-login.py "$PORT" good \
    && echo "  ok    password login, whoami and larder over the channel" \
    || { echo "  FAIL  interactive login did not work"; fail=1; }
python3 scripts/ssh-login.py "$PORT" bad \
    && echo "  ok    a wrong password is refused" \
    || { echo "  FAIL  wrong password was not refused"; fail=1; }
python3 scripts/ssh-login.py "$PORT" exec \
    && echo "  ok    'ssh host whoami' ran one command and closed" \
    || { echo "  FAIL  exec did not run and close"; fail=1; }
python3 scripts/ssh-login.py "$PORT" key "$KEYDIR/id" \
    && echo "  ok    an ed25519 key in /AUTHKEYS logs in without a password" \
    || { echo "  FAIL  key login failed"; fail=1; }
python3 scripts/ssh-login.py "$PORT" keyas "$KEYDIR/sid" saucier \
    && python3 scripts/ssh-login.py "$PORT" keydeny "$KEYDIR/sid" headchef \
    && echo "  ok    saucier's key opens saucier's account and not the headchef's" \
    || { echo "  FAIL  a cook's key and the accounts it opens"; fail=1; }
python3 scripts/ssh-login.py "$PORT" keydeny "$KEYDIR/id" saucier \
    && echo "  ok    a bare key is the headchef's only, not everyone's" \
    || { echo "  FAIL  a bare key opened another cook's account"; fail=1; }
python3 scripts/ssh-login.py "$PORT" busy \
    && echo "  ok    with four sessions open, a fifth client is told the vault is busy" \
    || { echo "  FAIL  fifth client was not told busy"; fail=1; }
python3 scripts/ssh-login.py "$PORT" pair \
    && echo "  ok    two sessions at once, as two cooks, each with its own cwd, and Ctrl-C in one spares the other" \
    || { echo "  FAIL  two concurrent sessions"; fail=1; }
python3 scripts/ssh-login.py "$PORT" execfail \
    && echo "  ok    an unknown command over exec exits 127" \
    || { echo "  FAIL  exec status for an unknown command"; fail=1; }
python3 scripts/ssh-login.py "$PORT" isolate \
    && echo "  ok    a session has its own cwd, and clockout ends it" \
    || { echo "  FAIL  session isolation"; fail=1; }
grep -q "zebracake" "$LOG" \
    && { echo "  FAIL  the session's output reached the console"; fail=1; } \
    || echo "  ok    none of the session's output reached the console"
python3 scripts/ssh-login.py "$PORT" ctrlc \
    && echo "  ok    Ctrl-C over the wire kills the session's program" \
    || { echo "  FAIL  Ctrl-C over SSH"; fail=1; }
before=$(grep -cE '/glutton\.elf exited with code -137' "$LOG")
python3 scripts/ssh-login.py "$PORT" hangup
after=$(grep -cE '/glutton\.elf exited with code -137' "$LOG")
[ "$after" -gt "$before" ] \
    && echo "  ok    hanging up kills the session's background job" \
    || { echo "  FAIL  a hung-up session's job survived ($before -> $after)"; fail=1; }
# Every session above ran with the vault rekeying itself every 8 packets out.
# (Output goes in kilobyte CHANNEL_DATA packets, so a whole login is only a
# dozen packets: a threshold of 60 was never reached, which is how this check
# first failed.)
python3 scripts/ssh-login.py "$PORT" rekey \
    && echo "  ok    a session survives the client rekeying every 2 KB" \
    || { echo "  FAIL  client-initiated rekey"; fail=1; }
grep -q "rekeyed (client)" "$LOG" && echo "  ok    the vault accepted a client's rekey" \
    || { echo "  FAIL  no client rekey in the kernel log"; fail=1; }
grep -q "rekeyed (server)" "$LOG" && echo "  ok    the vault started rekeys of its own" \
    || { echo "  FAIL  no server rekey in the kernel log"; fail=1; }
grep -q "authenticated by key" "$LOG" && echo "  ok    the kernel logged the key login" \
    || { echo "  FAIL  no key login in the kernel log"; fail=1; }
halt; rm -f "$LOG" "$MON"

boot
kfp2=$(grep -oE "^\[ssh\] host key SHA256:[A-Za-z0-9+/]+" "$LOG" | awk '{print $4}')
grep -q "new host key written" "$LOG" \
    && { echo "  FAIL  host key regenerated on reboot"; fail=1; } \
    || echo "  ok    host key loaded from disk on reboot"
[ -n "$kfp1" ] && [ "$kfp1" = "$kfp2" ] \
    && echo "  ok    fingerprint unchanged across reboot" || { echo "  FAIL  fingerprint changed ('$kfp1' -> '$kfp2')"; fail=1; }
exit $fail

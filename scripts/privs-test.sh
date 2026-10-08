#!/usr/bin/env bash
# privs-test.sh - what an ordinary cook may not do (v0.55.0).
#
# Proven on v0.54.2: the cook closed the vault the headchef had opened,
# changed its settings and rebooted the machine; and the cook's `hatch 80`
# served /etc/kitchen and the vault's private host key to anyone on the
# network. Now the headchef's commands refuse a cook (bare status still
# works, and `chef` still elevates), and hatch serves only public files.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18210
fail=0
WORK=$(mktemp -d /tmp/soupos-privs.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:80" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys() { python3 scripts/qemu_keys.py "$WORK/mon.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }; }
keys "headchef" "rosemary" "WAIT:1" "vault 22" "WAIT:2" "stir /PUBLIC.TXT anyone may read this" "WAIT:1" "clockout" "WAIT:1" \
     "cook" "soup" "WAIT:2" "vault off" "WAIT:1" "vault rekey 5" "WAIT:1" "vault idle 3" "WAIT:1" "pass 23" "WAIT:1" \
     "spill" "WAIT:1" "freeze" "WAIT:1" "reheat" "WAIT:1" "fatstress" "WAIT:1" "bowltest" "WAIT:1" "vault" "WAIT:1" \
     "chef whoami" "WAIT:1" "wrongone" "WAIT:3" "chef whoami" "WAIT:1" "wrongtwo" "WAIT:4" \
     "chef vault idle 0" "WAIT:1" "rosemary" "WAIT:2" "hatch 80" "WAIT:2"
code() { curl -s -o "$WORK/body" -w '%{http_code}' -m 10 "http://127.0.0.1:$PORT$1"; }
k=$(code /etc/kitchen); h=$(code /HOSTKEY.ED); pub=$(code /PUBLIC.TXT); body=$(cat "$WORK/body")
[ "$k" = 403 ] && [ "$h" = 403 ] \
    && echo "  ok    the cook's hatch refuses the roster and the host key (403, 403)" \
    || { echo "  FAIL  hatch served a private file: kitchen $k, host key $h"; fail=1; }
[ "$pub" = 200 ] && [ "$body" = "anyone may read this" ] \
    && echo "  ok    and still serves a public file" || { echo "  FAIL  public file: $pub '$body'"; fail=1; }
keys "TYPE:x" "WAIT:2" "whoami" "WAIT:1"
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
n=$(echo "$L" | grep -cE "^\[shell\] cook may not ")
[ "$n" = 9 ] && echo "  ok    all nine of the headchef's commands refused the cook, each logged" \
    || { echo "  FAIL  $n of 9 refusals logged:"; echo "$L" | grep -E "^\[shell\] cook may not " | sed 's/^/        /'; fail=1; }
! echo "$L" | grep "^\[ssh\] stopped" >/dev/null && ! echo "$L" | grep "pass open\|the pass is open" >/dev/null \
    && echo "  ok    the vault stayed open and the pass stayed shut" || { echo "  FAIL  a door changed"; fail=1; }
echo "$L" | grep "vault open on port 22" >/dev/null \
    && echo "  ok    a cook may still ask the vault's status" || { echo "  FAIL  bare vault refused"; fail=1; }
echo "$L" | grep "the vault never ends an idle session" >/dev/null \
    && echo "  ok    chef still runs one as the headchef" || { echo "  FAIL  chef vault idle 0"; fail=1; }
[ "$(echo "$L" | grep -c "kernel_main")" = 1 ] && echo "$L" | grep -E "cook  \(uid 1\)" >/dev/null \
    && echo "  ok    the machine never went down, and the cook carries on" || { echo "  FAIL  the machine rebooted or stopped"; fail=1; }
# chef is a door too (v0.55.2): measured on v0.55.1, 137 wrong guesses a
# minute through it, none recorded.
echo "$L" | grep "cook: wrong headchef secret at chef, 1000 ms pause" >/dev/null && echo "$L" | grep "cook: wrong headchef secret at chef, 2000 ms pause" >/dev/null \
    && echo "  ok    wrong secrets at chef cost 1 s, then 2 s" || { echo "  FAIL  chef pauses"; fail=1; }
R=$(mtype -i "$WORK/disk.img" ::/etc/logins 2>/dev/null)
[ "$(echo "$R" | grep -cE "refused +headchef +chef:cook")" = 2 ] && echo "$R" | grep -E "ok +headchef +chef:cook" >/dev/null \
    && echo "  ok    chef's refusals and its success are in the login record, naming the cook" || { echo "  FAIL  chef in /etc/logins"; fail=1; }
exit $fail

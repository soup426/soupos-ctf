#!/usr/bin/env bash
# vars-test.sh - shell variables, against sh (v0.58.0).
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18250
fail=0
WORK=$(mktemp -d /tmp/soupos-vars.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/V.TXT" <<'SCRIPT'
A=soup
B="two  words"
cook call.elf $A ${A}y "$B" '$A' $UNSET end > /V1.TXT
A=$A$A
cook call.elf $A > /V2.TXT
discard A
cook call.elf [$A] > /V3.TXT
C=x ; cook call.elf $C$C > /V4.TXT
cook greet.elf ; S=$? ; cook call.elf $S > /V5.TXT
SCRIPT
mcopy -i "$WORK/disk.img" "$WORK/V.TXT" ::/V.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /V.TXT" "WAIT:5" \
    "K=console" "WAIT:1" "vault 22" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
# A session's variables are its own: set K there, and the console's K is untouched.
python3 - "$PORT" <<'PY' || fail=1
import pexpect, sys
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no"
c = pexpect.spawn(f"ssh -tt -p {sys.argv[1]} {o} headchef@127.0.0.1", encoding="utf-8", timeout=30)
c.expect("password:"); c.sendline("rosemary"); c.expect("Welcome"); c.expect("> ")
c.sendline("cook call.elf [$K] > /V6.TXT"); c.expect("> ")
c.sendline("K=session"); c.expect("> ")
c.sendline("clockout"); c.close(force=True)
PY
python3 scripts/qemu_keys.py "$WORK/mon.sock" "cook call.elf \$K > /V7.TXT" "WAIT:2" >/dev/null 2>&1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; ( cd "$WORK/host" && sed -e 's|cook greet.elf|(exit 42)|g' -e 's|cook call.elf|echo|g' -e 's| /V| V|g' -e 's|^discard |unset |' "$WORK/V.TXT" > v.sh && sh v.sh ) >/dev/null 2>&1
bad=0
for n in 1 2 3 4 5; do
    g=$(mcopy -n -i "$WORK/disk.img" "::/V$n.TXT" - 2>/dev/null); w=$(cat "$WORK/host/V$n.TXT" 2>/dev/null)
    [ "$g" = "$w" ] || { echo "        V$n: [$g]  sh [$w]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    a script's variables match sh: \$A \${A}y \"\$B\" '\$A' unset-is-empty, A=\$A\$A, unset, ; between, S=\$?" \
    || { echo "  FAIL  variables differ from sh"; fail=1; }
[ "$(mcopy -n -i "$WORK/disk.img" ::/V6.TXT - 2>/dev/null)" = "[]" ] && [ "$(mcopy -n -i "$WORK/disk.img" ::/V7.TXT - 2>/dev/null)" = "console" ] \
    && echo "  ok    each shell has its own: the session did not see the console's K, nor change it" || { echo "  FAIL  variables leaked between shells"; fail=1; }
exit $fail

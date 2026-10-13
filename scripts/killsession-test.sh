#!/usr/bin/env bash
# killsession-test.sh - killing a session's task while it runs a script does
# not leave the session flagged (v0.60.312, review R-K37). v0.60.300 sent any
# task with ->soupyc set down the deferred kill, and soupyc_run sets it on
# the shell's own task for a foreground soup too: the session got kill_req,
# nothing cleared it, every later soup there failed "killed" and every pipe
# read was EOF, and the killer was told it "is stopping" for good. Only a
# spawn()ed script's task takes the deferred kill now; a session's is killed
# as before, and kill_req is cleared when a script returns. Here an SSH
# session runs a sleeping loop, the console kills the session's task, and
# then the session is gone ("killed task N" and not in ps), or a script
# there runs.
cd "$(dirname "$0")/.."
PORT=18290
WORK=$(mktemp -d /tmp/soupos-killsession.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
printf 'while 1 {\n    sleep(200)\n}\n' > "$WORK/loop.sc"; printf 'pour "HI" + "THERE"\n' > "$WORK/hi.sc"
mcopy -i "$WORK/disk.img" "$WORK/loop.sc" ::/LOOP.SC; mcopy -i "$WORK/disk.img" "$WORK/hi.sc" ::/HI.SC
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5
timeout 60 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "vault 22" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
python3 - "$PORT" "$WORK" <<'PY' || fail=1
import os, re, subprocess, sys, time
import pexpect
port, work = sys.argv[1], sys.argv[2]
def console(*keys):
    subprocess.run(["timeout", "60", "python3", "scripts/qemu_keys.py", f"{work}/mon.sock", *keys],
                   env=dict(os.environ, QEMU_KEYS_NOWAIT="1"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def log(): return open(f"{work}/log", errors="replace").read().replace("\r", "")
c = pexpect.spawn(f"ssh -p {port} -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o NumberOfPasswordPrompts=1 "
                  f"-o PubkeyAuthentication=no headchef@127.0.0.1", encoding="utf-8", timeout=25)
c.expect("password:"); c.sendline("rosemary"); c.expect("a shell of your own")
c.sendline("soup /LOOP.SC"); time.sleep(2)
before = len(log())
console("ps", "WAIT:2")
m = re.findall(r"^\s*(\d+)\s.*sh:session", log()[before:], re.M)
if not m: print("  FAIL  ps shows no sh:session task"); sys.exit(1)
console(f"kill {m[-1]}", "WAIT:7")
c.sendline("soup /HI.SC")
try:
    i = c.expect(["HITHERE", "killed", pexpect.EOF, pexpect.TIMEOUT], timeout=10)
except Exception:
    i = 2
# Gone: the console said so and ps lists it no more (the SSH channel itself
# outliving a killed session is the vault's, not this).
tail = log()[before:]
gone = f"killed task {m[-1]}" in tail
if gone:
    mark = len(log()); console("ps", "WAIT:2")
    gone = not re.search(rf"^\s*{m[-1]}\s.*sh:session", log()[mark:], re.M)
ok = i == 0 or ((i == 2 or i == 3) and gone)
print(("  ok    " if ok else "  FAIL  ") + ["after the kill a script in the session runs", "after the kill the session's scripts fail \"killed\"",
       "the kill ended the session", "after the kill the session neither ran the script nor ended"][i]
      + ("; the console said killed and ps lists it no more" if gone else ""))
c.close(force=True)
sys.exit(0 if ok else 1)
PY
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
grep 'KERNEL PANIC' "$WORK/log" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
[ "$fail" = 0 ] && echo "killsession-test: PASS" || echo "killsession-test: FAIL"
exit "$fail"

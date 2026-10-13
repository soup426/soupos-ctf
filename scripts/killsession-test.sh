#!/usr/bin/env bash
# killsession-test.sh - killing a session's task while it runs a script stops
# the script, and leaves neither the session flagged nor the disk held
# (v0.60.312, review R-K37; v0.60.320, reviews R-K42, R-K43). v0.60.300's
# deferred kill left a session's kill_req set for good (every later soup
# there failed "killed"); v0.60.312 sent sessions back to task_kill, and a
# session killed inside close() died holding fat_mtx: every later write on
# the machine hung, and its buffered writes were never given back. Now any
# task running a script is asked: the script stops at its next statement
# and closes its own files, the session's kill_req is cleared when it
# returns, and the session carries on. Here an SSH session's script writes
# 12 MB and closes; the console kills its task just after CLOSING. Then a
# console script that writes 1 MB and closes finishes, the session runs a
# script again, and the big file holds all it wrote.
cd "$(dirname "$0")/.."
PORT=18290
WORK=$(mktemp -d /tmp/soupos-killsession.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
cat > "$WORK/big.sc" <<'SC'
let f = open("/big.txt", "w")
let s = "xxxxxxxx"
for k in 1..10 {
    s = s + s
}
for k in 1..3072 {
    write(f, s)
}
pour "CLO" + "SING"
close(f)
pour "CLO" + "SED"
SC
cat > "$WORK/con.sc" <<'SC'
let f = open("/con.txt", "w")
let s = "xxxxxxxx"
for k in 1..10 {
    s = s + s
}
for k in 1..257 {
    write(f, s)
}
close(f)
pour "CON" + "DONE"
SC
printf 'pour "HI" + "THERE"\n' > "$WORK/hi.sc"
for f in big con hi; do mcopy -i "$WORK/disk.img" "$WORK/$f.sc" "::/$(echo $f | tr a-z A-Z).SC"; done
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
def console(*keys, t=60):
    subprocess.run(["timeout", str(t), "python3", "scripts/qemu_keys.py", f"{work}/mon.sock", *keys],
                   env=dict(os.environ, QEMU_KEYS_NOWAIT="1"), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def log(): return open(f"{work}/log", errors="replace").read().replace("\r", "")
ok = True
def check(cond, what):
    global ok
    print(("  ok    " if cond else "  FAIL  ") + what); ok &= bool(cond)
c = pexpect.spawn(f"ssh -p {port} -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o NumberOfPasswordPrompts=1 "
                  f"-o PubkeyAuthentication=no headchef@127.0.0.1", encoding="utf-8", timeout=25)
c.expect("password:"); c.sendline("rosemary"); c.expect("a shell of your own")
before = len(log())
console("ps", "WAIT:2")
m = re.findall(r"^\s*(\d+)\s.*sh:session", log()[before:], re.M)
if not m: print("  FAIL  ps shows no sh:session task"); sys.exit(1)
tid = m[-1]
c.sendline("soup /BIG.SC")
c.expect("CLOSING", timeout=60)
console(f"kill {tid}", "WAIT:3")
mark = len(log())
console("soup /CON.SC", "UNTIL:CONDONE", t=70)   # behind the killed script's 12 MB flush; a hang never ends
check("CONDONE" in log()[mark:], "after the kill the console's write and close finish (the disk is not held)")
c.sendline("soup /HI.SC")
try: c.expect("soup /HI.SC", timeout=15)          # past the killed script's own "killed", to this line's echo
except Exception: pass
i = c.expect(["HITHERE", "killed", pexpect.EOF, pexpect.TIMEOUT], timeout=15)
check(i == 0, "after the kill a script in the session runs again" if i == 0 else
      ["", "after the kill the session's scripts fail \"killed\"", "the kill ended the session", "after the kill the session does nothing"][i])
c.close(force=True)
sys.exit(0 if ok else 1)
PY
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
sz=$(mcopy -n -i "$WORK/disk.img" ::/big.txt - 2>/dev/null | wc -c)
[ "$sz" = 12578816 ] && echo "  ok    the killed script's file holds all it wrote: $sz bytes" || { echo "  FAIL  /big.txt is ${sz:-no} bytes (want 12578816)"; fail=1; }
grep 'KERNEL PANIC' "$WORK/log" >/dev/null && { echo "  FAIL  a panic"; fail=1; }
[ "$fail" = 0 ] && echo "killsession-test: PASS" || echo "killsession-test: FAIL"
exit "$fail"

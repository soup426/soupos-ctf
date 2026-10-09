#!/usr/bin/env bash
# clip-test.sh - one clipboard per cook (v0.60.13).
#
# The headchef cuts at the console; the saucier, logged in over SSH at the
# same time, must find an empty clipboard and cut their own; the console
# must still hold the headchef's; a second session of the headchef's shares
# it, as a person would expect of one cook.
set -uo pipefail
cd "$(dirname "$0")/.."
PORT=18270
fail=0
WORK=$(mktemp -d /tmp/soupos-clip.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -netdev "user,id=n0,hostfwd=tcp:127.0.0.1:$PORT-:22" -device rtl8139,netdev=n0 \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
keys() { python3 scripts/qemu_keys.py "$WORK/mon.sock" "$@" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }; }
keys "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" \
     "TYPE:slurp chervil" "KEY:ctrl-u" "KEY:ctrl-c" "vault 22" "WAIT:2"
python3 - "$PORT" <<'PY' || fail=1
import pexpect, sys
port = sys.argv[1]
o = "-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no -o ConnectTimeout=10 -o NumberOfPasswordPrompts=1"
PROMPT = r"\x1b\[0;97m> "
def session(who, pw):
    c = pexpect.spawn(f"ssh -tt -p {port} {o} {who}@127.0.0.1", encoding="utf-8", timeout=40)
    c.expect("password:"); c.sendline(pw); c.expect(PROMPT)
    return c
bad = 0
def report(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + what)
    if not ok: bad = 1
s = session("saucier", "basil")
s.sendline("scraps"); s.expect(PROMPT)
report("clipboard empty" in s.before and "chervil" not in s.before,
       "the saucier, over SSH, does not get the headchef's clipboard")
s.send("slurp tarte"); s.send("\x15"); s.send("\x03"); s.expect(PROMPT)
s.sendline("scraps"); s.expect(PROMPT)
report("slurp tarte" in s.before, "the saucier's own cut is on the saucier's clipboard")
h = session("headchef", "rosemary")
h.sendline("scraps"); h.expect(PROMPT)
report("slurp chervil" in h.before and "tarte" not in h.before,
       "a second session of the headchef's shares the headchef's clipboard")
s.close(force=True); h.close(force=True)
sys.exit(bad)
PY
keys "scraps" "WAIT:2" "fire saucier" "WAIT:3" "hire saucier" "WAIT:1" "thyme" "WAIT:1" "thyme" "WAIT:2" "clockout" "WAIT:1" \
     "saucier" "thyme" "WAIT:2" "scraps" "WAIT:2"
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
last_clip() { grep -E '^\[clip\] ' "$WORK/out.log" | grep -v '\] set ' | tail -n "$1" | head -1; }
case "$(last_clip 2)" in *"slurp chervil"*) echo "  ok    the console still holds the headchef's cut";;
    *) echo "  FAIL  the console's clipboard: '$(last_clip 2)'"; fail=1;; esac
case "$(last_clip 1)" in "[clip] empty") echo "  ok    a cook hired into a fired cook's uid starts with an empty clipboard";;
    *) echo "  FAIL  after fire and hire: '$(last_clip 1)'"; fail=1;; esac
exit $fail

#!/usr/bin/env bash
# keys-until-test.sh - qemu_keys' UNTIL after a KEY: or TYPE: item waits for
# what comes after those keys (v0.60.255, review R-T5). No QEMU: a fake
# monitor, started with -serial file:LOG on its command line as QEMU is,
# echoes a typed line and a prompt at once, and answers Tab (KEY:) or a
# typed x (TYPE:) with a second prompt only 2 s later. UNTIL:PROMPT$ after
# the keys must wait for that one; before the fix it took the first.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-keysuntil.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cat > "$WORK/fake.py" <<'PY'
import socket, sys, os, time, threading
mon, log = sys.argv[1], sys.argv[3][5:]
srv = socket.socket(socket.AF_UNIX); srv.bind(mon); srv.listen(1)
c, _ = srv.accept(); c.sendall(b'QEMU 0.0 monitor\n')
def put(b):
    with open(log, 'ab') as f: f.write(b)
line = b''; buf = b''
while True:
    d = c.recv(4096)
    if not d: break
    buf += d
    while b'\n' in buf:
        cmdl, buf = buf.split(b'\n', 1)
        c.sendall(b'(qemu) ')
        if not cmdl.startswith(b'sendkey '): continue
        k = cmdl[8:]
        if k == b'ret': put(line + b'\nPROMPT$ '); line = b''
        elif k in (b'tab', b'x'): threading.Timer(2.0, put, [b'\nPROMPT$ ']).start()
        elif len(k) == 1: line += k
PY
for item in "KEY:tab" "TYPE:x"; do
    rm -f "$WORK/mon.sock"; : > "$WORK/log"
    python3 "$WORK/fake.py" "$WORK/mon.sock" -serial "file:$WORK/log" & PID=$!
    t0=$(date +%s%3N)
    QEMU_KEYS_NOWAIT=1 python3 scripts/qemu_keys.py "$WORK/mon.sock" "ls" "$item" "UNTIL:PROMPT\$" >/dev/null 2>&1 || { echo "        $item: the driver failed"; fail=1; }
    took=$(( $(date +%s%3N) - t0 ))
    kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
    if [ "$took" -lt 2600 ]; then echo "        $item: UNTIL returned after ${took} ms, on the prompt from before the keys"; fail=1; fi
done
[ "$fail" = 0 ] && echo "keys-until-test: PASS" || echo "keys-until-test: FAIL"
exit "$fail"

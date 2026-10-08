#!/usr/bin/env bash
# lsl-test.sh - peek.elf -l against serve and mtools (v0.59.1).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-lsl.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" \
    "slurp ==/" "WAIT:1" "cd /" "WAIT:1" "serve" "WAIT:2" "cook peek.elf -l /" "WAIT:3" \
    "slurp ==/etc" "WAIT:1" "cd /etc" "WAIT:1" "serve" "WAIT:1" "cook peek.elf -l /etc" "WAIT:2" \
    "slurp ==/home" "WAIT:1" "cd /home" "WAIT:1" "serve" "WAIT:1" "cook peek.elf -l /home" "WAIT:2" \
    "cd /" "WAIT:1" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" "slurp ==cook" "WAIT:1" "cook peek.elf -l /etc" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
python3 - "$WORK/log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], errors="replace").read()
parts = re.split(r"^==(\S+)$", log, flags=re.M)
sections = dict(zip(parts[1::2], parts[2::2]))
fail = 0
for d in ["/", "/etc", "/home"]:
    sec = sections.get(d, "")
    serve = set()
    for m in re.finditer(r"^  ([rwx-]{6})  (\S+)\s+(.+?)\s+(\d+) B$|^  ([rwx-]{6})  (\S+)\s+(.+?)\s+<bowl>$", sec, re.M):
        if m.group(1): serve.add((m.group(1), m.group(2), m.group(3).rstrip(), int(m.group(4))))
        else:          serve.add((m.group(5), m.group(6), m.group(7).rstrip(), -1))
    lsl = set((m.group(1), m.group(2), m.group(4), int(m.group(3)))
              for m in re.finditer(r"^LSL ([rwx?-]{6}) (\S+) (-?\d+) (.+)$", sec, re.M))
    lsl = {t for t in lsl if len(t[2]) <= 30}            # serve cuts names past 30
    if serve and serve == lsl:
        print(f"  ok    peek.elf -l {d} agrees with serve on every entry's mode, owner, name and size ({len(lsl)})")
    else:
        print(f"  FAIL  peek.elf -l {d} vs serve: only serve {sorted(serve - lsl)[:3]} only ls {sorted(lsl - serve)[:3]}"); fail = 1
cook = sections.get("cook", "")
if re.search(r"^LSL rw---- headchef 349 kitchen$|^LSL rw---- headchef \d+ kitchen$", cook, re.M):
    print("  ok    for the cook, ls -l shows /etc/kitchen (stat needs no read on it)")
else:
    print("  FAIL  the cook's ls -l /etc"); fail = 1
sys.exit(fail)
PY
# Sizes against mtools for /etc's files, from the last listing (the cook's, after
# every login has been added to /etc/logins).
bad=0
for f in kitchen logins; do
    m=$(mcopy -n -i "$WORK/disk.img" "::/etc/$f" - 2>/dev/null | wc -c)
    l=$(grep -oE "^LSL [rwx-]{6} \S+ [0-9]+ $f$" "$WORK/log" | tail -1 | awk '{print $4}')
    [ "$m" = "$l" ] || { echo "        /etc/$f: ls -l $l, mtools $m"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    the sizes agree with mtools" || { echo "  FAIL  sizes"; fail=1; }
exit $fail

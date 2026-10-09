#!/usr/bin/env bash
# Does a ring-3 program list a directory the way something else reads it?
#
# peek.elf walks the root through SYS_READDIR and logs one "LS <d|f> <size>
# <name>" line per entry. Afterwards mtools lists the same image, and the two
# must agree on every name (the long one where there is one), size and type.
# soupOS's own `serve` would only prove soupOS agrees with itself.
set -uo pipefail
# v0.60.124: each listing waits for the prompt, not two seconds: SYS_READDIR
# reads the whole directory for every entry, so a listing grows with the
# disk, and with 79 entries under a loaded full check (2026-10-09) peek.elf
# was cut off at 49 when the wait ran out.
cd "$(dirname "$0")/.."

TESTDISK=$(mktemp /tmp/soupos-lsdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-ls.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-ls.XXXXXX.sock)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host \
    -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "cook peek.elf /" "UNTIL:@soupOS:" "cook peek.elf /nowhere" "UNTIL:@soupOS:" >/dev/null 2>&1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

python3 - "$LOG" "$TESTDISK" <<'PY'
import re, subprocess, sys
log, img = sys.argv[1], sys.argv[2]
ours = {}
for line in open(log, errors="replace"):
    m = re.match(r"LS ([df]) (\d+) (.+?)\s*$", line)
    if m: ours[m.group(3)] = (m.group(1), int(m.group(2)) if m.group(1) == "f" else 0)
theirs = {}
for line in subprocess.run(["mdir", "-i", img, "::/"], capture_output=True, text=True).stdout.splitlines():
    m = re.match(r"^(\S+)\s+(\S*)\s+(<DIR>|\d+)\s+\d{4}-\d\d-\d\d\s+\d+:\d\d\s*(.*)$", line)
    if not m or m.group(1) in (".", ".."): continue
    long = m.group(4).strip()
    name = long or (m.group(1) + ("." + m.group(2) if m.group(2) else ""))
    theirs[name] = ("d", 0) if m.group(3) == "<DIR>" else ("f", int(m.group(3)))
fail = 0
end = re.search(r"LS end (\d+) entries", open(log, errors="replace").read())
if end and int(end.group(1)) == len(ours) == len(theirs):
    print(f"  ok    peek.elf and mtools both see {len(ours)} entries")
else:
    print(f"  FAIL  entry counts differ: peek.elf {len(ours)} (said {end.group(1) if end else '?'}), mtools {len(theirs)}"); fail = 1
diff = {k for k in set(ours) | set(theirs) if ours.get(k) != theirs.get(k)}
if not diff:
    print("  ok    every name, size and type agrees, long names included")
else:
    for k in sorted(diff)[:6]: print(f"  FAIL  {k!r}: peek.elf {ours.get(k)} mtools {theirs.get(k)}")
    fail = 1
if "LS error: no such directory" in open(log, errors="replace").read():
    print("  ok    a missing directory is an error, not an empty listing")
else:
    print("  FAIL  a missing directory did not report an error"); fail = 1
sys.exit(fail)
PY

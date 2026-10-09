#!/usr/bin/env bash
# du-test.sh - du agrees with the disk (v0.53.4).
#
# mtools' mdu is not the judge: it never counts a bowl's own cluster and
# does count each `..` entry as if it were a file (measured 2026-10-08:
# ::/etc 2 for two one-cluster files; DEEP 2 for one). The judges are the
# FAT itself, read on the host: du's total for / must equal the clusters in
# use (which fsck.fat also reports), and every bowl's clusters and bytes
# must equal a walk of the image. A cook who may not read a bowl sees it
# marked, not counted.
set -uo pipefail
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-du.XXXXXX)
PID=""
fail=0
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 5
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2" \
    "stir /home/saucier/NOTES.TXT a few words of notes" "WAIT:1" "mkbowl /home/saucier/deep" "WAIT:1" \
    "stir /home/saucier/deep/A.TXT aaaa" "WAIT:1" "clockout" "WAIT:1" \
    "intern" "mint" "WAIT:2" "portions /home" "WAIT:2" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "portions /" "WAIT:3" "portions /home" "WAIT:2" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
# The headchef's du runs last: every login appends to /etc/logins, so a
# login after it would make the image newer than what du measured.
sleep 1
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/s.txt"

python3 - "$WORK/disk.img" "$WORK/s.txt" <<'PY' || fail=1
import re, struct, subprocess, sys
img, log = sys.argv[1], open(sys.argv[2]).read()
d = open(img, 'rb').read()
bps, spc, res, nf, rootn = struct.unpack_from('<HBHBH', d, 11); fatsz = struct.unpack_from('<H', d, 22)[0]
tot = struct.unpack_from('<H', d, 19)[0] or struct.unpack_from('<I', d, 32)[0]
fat = res*bps; root = (res + nf*fatsz)*bps; data = root + rootn*32; cb = spc*bps
nclus = (tot - (res + nf*fatsz + rootn*32//bps)) // spc
nxt = lambda c: struct.unpack_from('<H', d, fat + 2*c)[0]
def chain(c):
    n = 0
    while 2 <= c < 0xFFF8: n += 1; c = nxt(c)
    return n
def clusters_of(c):
    out = []
    while 2 <= c < 0xFFF8: out.append(c); c = nxt(c)
    return out
def entries(first):        # (name, attr, first cluster, size); the long name when it has one
    offs = [(root, rootn)] if first == 0 else [(data + (c-2)*cb, cb//32) for c in clusters_of(first)]
    parts = {}
    for off, count in offs:
        for i in range(count):
            e = d[off+32*i: off+32*i+32]
            if e[0] == 0: return
            if e[0] == 0xE5: parts = {}; continue
            if e[11] == 0x0F:                       # a piece of a long name
                raw = e[1:11] + e[14:26] + e[28:32]
                parts[e[0] & 0x1F] = raw.decode('utf-16-le', 'replace')
                continue
            if e[11] & 0x08: parts = {}; continue
            name = e[:11].decode('latin1')
            if parts:
                name = ''.join(parts[k] for k in sorted(parts)).split('\x00')[0].split('\uffff')[0]
            parts = {}
            if name.startswith(('.  ', '.. ')): continue
            yield name, e[11], struct.unpack_from('<H', e, 26)[0], struct.unpack_from('<I', e, 28)[0]
def walk(first):           # (clusters, bytes) under a bowl, not its own
    cl = by = 0
    for name, attr, c, sz in entries(first):
        cl += chain(c)
        if attr & 0x10: a, b = walk(c); cl += a; by += b
        else: by += sz
    return cl, by
fail = 0
def say(ok, text):
    global fail
    print(("  ok    " if ok else "  FAIL  ") + text); fail |= (not ok)
used = sum(1 for c in range(2, nclus + 2) if nxt(c) != 0)
fsck = subprocess.run(["fsck.fat", "-n", img], capture_output=True, text=True).stdout
fsck_used = int(re.search(r"files, (\d+)/\d+ clusters", fsck).group(1))
tot_line = re.search(r"^\s+(\d+)\s+(\d+)\s+total for /$", log, re.M)
rc, rb = walk(0)
say(tot_line and int(tot_line.group(1)) == used == fsck_used == rc,
    f"du / totals {tot_line.group(1) if tot_line else '?'} clusters: the FAT has {used} in use, fsck says {fsck_used}")
say(tot_line and int(tot_line.group(2)) == rb, f"du / totals {tot_line.group(2) if tot_line else '?'} bytes: the image's files hold {rb}")
home = next(c for n, a, c, s in entries(0) if n.strip() == "home")   # long names since v0.57.6
sauc = next(c for n, a, c, s in entries(home) if n.strip() == "saucier")
sc, sb = walk(sauc); sc += chain(sauc)
m = ([None] + list(re.finditer(r"^\s+(\d+)\s+(\d+)\s+saucier/$", log, re.M)))[-1]
say(m and (int(m.group(1)), int(m.group(2))) == (sc, sb), f"saucier/ is {m.group(1) if m else '?'} clusters, {m.group(2) if m else '?'} bytes: the image says {sc}, {sb}")
hc, hb = walk(home); hc += chain(home)
m = ([None] + list(re.finditer(r"^\s+(\d+)\s+(\d+)\s+total for /home$", log, re.M)))[-1]
say(m and (int(m.group(1)), int(m.group(2))) == (hc, hb), f"/home is {m.group(1) if m else '?'} clusters with its own: the image says {hc}")
say(re.search(r"saucier/  \(some not yours to read\)", log) is not None, "intern's du marks saucier's closed home instead of counting it")
sys.exit(fail)
PY
[ "$fail" = 0 ] || exit 1
exit 0

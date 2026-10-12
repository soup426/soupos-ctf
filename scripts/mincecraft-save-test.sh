#!/usr/bin/env bash
# mincecraft-save-test.sh - Mincecraft's world file (v0.60.197). Four boots
# on one disk copy, one at a time:
#   1. the flat world, -world /SAVE.WLD (none yet: a new world): walk, look
#      down, break, P saves, turn, Esc saves again and ends it
#   2. -world /SAVE.WLD again: loaded, the same hash, place and inventory
#      as the last save, the first tick where it was saved
#   on the host: /BAD.WLD (byte 100 flipped), /SHORT.WLD (10 bytes short),
#   /LONG.WLD (four bytes more), /T.WLD (3 MB of zeros)
#   3. -check refuses BAD, SHORT and LONG, each by name; then the game on
#      -flat -world /T.WLD refuses it, plays on a new world, and P writes a
#      small save over the 3 MB file
#   4. -check /T.WLD: loaded with that save's hash (a tail left from the
#      long file would fail the end-of-file check, so this proves O_WRITE
#      truncated it); -check with no -world looks for HOME's
#      /mincecraft.wld (the headchef's HOME is /) and finds none
# (v0.60.202) and: a save with the feet above y 64 loads; the refused 3 MB
# file is kept whole as /T.WLD.bad; a disk too full for a save says so
# and the first Esc does not quit.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mcsave.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
boot() {   # $1: name, then the keys after the login
    local n=$1; shift
    local keys=("headchef" "rosemary" "WAIT:1" "$@")
    qemu-system-i386 -accel kvm -cpu host -drive "file=${DISK:-$WORK/disk.img},format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$WORK/serial-$n.log" -monitor "unix:$WORK/mon-$n.sock,server,nowait" >/dev/null 2>&1 &
    PID=$!
    python3 scripts/qemu_keys.py "$WORK/mon-$n.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU ($n)"; fail=1; }
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
boot save "cook mincecraft.elf -flat -script -wait -world /SAVE.WLD &" "WAIT:1" "countertop" "WAIT:5" \
          "KEY:w 600" "WAIT:1" "KEY:down 2000" "WAIT:1" "KEY:f 1000" "WAIT:2" "TYPE:p" "WAIT:2" \
          "KEY:right 500" "WAIT:1" "KEY:esc" "WAIT:4"
boot load "cook mincecraft.elf -script -wait -world /SAVE.WLD &" "WAIT:1" "countertop" "UNTIL:[mincecraft] window up" "WAIT:2" "KEY:esc" "WAIT:4"   # (v0.60.217) the window, not a fixed time: the hand is said after it opens
mcopy -n -i "$WORK/disk.img" ::SAVE.WLD "$WORK/good.wld" || { echo "  FAIL  no /SAVE.WLD on the disk"; fail=1; }
if [ -f "$WORK/good.wld" ]; then
    python3 -c "import sys;b=bytearray(open(sys.argv[1],'rb').read());b[100]^=1;open(sys.argv[2],'wb').write(b)" "$WORK/good.wld" "$WORK/bad.wld"
    head -c -10 "$WORK/good.wld" > "$WORK/short.wld"
    { cat "$WORK/good.wld"; printf 'JUNK'; } > "$WORK/long.wld"
    head -c 3000000 /dev/zero > "$WORK/t.wld"
    # Files made here (v0.60.202, v0.60.209): the sum or the CRC-32 at the end
    # is rewritten for the version in the header (1: a byte sum, 2: CRC-32).
    cat > "$WORK/craft.py" <<'CRAFT'
import struct, sys, zlib
def check_of(b): return zlib.crc32(bytes(b[:-4])) if struct.unpack('<I', b[4:8])[0] >= 2 else sum(b[:-4]) & 0xFFFFFFFF
def older(b, version):            # a version 4 file as 1 or 2 had it: 11 counts, not 14 (v0.60.215), no hearts
    b = bytearray(b[:80] + b[96:]); b[4:8] = struct.pack('<I', version); b[-4:] = struct.pack('<I', check_of(b)); return b
def three(b):                     # as version 3 had it (v0.60.221): 14 counts, no hearts
    b = bytearray(b[:92] + b[96:]); b[4:8] = struct.pack('<I', 3); b[-4:] = struct.pack('<I', check_of(b)); return b
good = bytearray(open(sys.argv[1], 'rb').read()); out = sys.argv[2]
print('version', struct.unpack('<I', good[4:8])[0], 'check ok', struct.unpack('<I', good[-4:])[0] == check_of(good))
# the feet at y 64.3, a jump off the ceiling's top block: play can write it, so it loads
b = bytearray(good); b[20:24] = struct.pack('<i', (64 << 16) + 21000); b[-4:] = struct.pack('<I', check_of(b))
open(out + '/high.wld', 'wb').write(b)
# version 1 (a byte sum) and version 2 (a CRC) files, 11 counts each: still read
open(out + '/v1.wld', 'wb').write(older(good, 1))
open(out + '/v2.wld', 'wb').write(older(good, 2))
open(out + '/v3.wld', 'wb').write(three(good))
# no hearts left (v0.60.221): a cook is never saved dead, so refused
b = bytearray(good); b[92:96] = struct.pack('<I', 0); b[-4:] = struct.pack('<I', check_of(b))
open(out + '/dead.wld', 'wb').write(b)
# two different (count, id) runs of the grid swapped (it starts at 96: "MINC",
# eight words, fourteen counts, the hearts): a valid grid, the same byte sum, another
# CRC; refused only by the CRC
b = bytearray(good)
i = next(i for i in range(96, len(b) - 8, 2) if b[i:i + 2] != b[i + 2:i + 4])
b[i:i + 2], b[i + 2:i + 4] = b[i + 2:i + 4], b[i:i + 2]
open(out + '/swap.wld', 'wb').write(b)
CRAFT
    python3 -I "$WORK/craft.py" "$WORK/good.wld" "$WORK" > "$WORK/craft.out"
    for f in bad short long t high v1 v2 v3 dead swap; do mcopy -o -i "$WORK/disk.img" "$WORK/$f.wld" "::${f^^}.WLD"; done
fi
boot check "cook mincecraft.elf -check -world /BAD.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /SHORT.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /LONG.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /V1.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /V2.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /V3.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /DEAD.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /SWAP.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -flat -script -wait -world /T.WLD &" "WAIT:1" "countertop" "WAIT:5" \
           "TYPE:p" "WAIT:2" "KEY:esc" "WAIT:4"
mcopy -n -i "$WORK/disk.img" ::T.WLD.bad "$WORK/t.bad" 2>/dev/null
mdir -i "$WORK/disk.img" ::BAD.WLD.bad >/dev/null 2>&1 && echo yes > "$WORK/bad-aside"
boot again "cook mincecraft.elf -check -world /T.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /HIGH.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check" "UNTIL:@soupOS:" \
           "hand HOME=/$(printf 'a%.0s' $(seq 150))" "UNTIL:@soupOS:" "cook mincecraft.elf -check" "UNTIL:@soupOS:"
# (v0.60.202) a disk with room for less than a save: P fails at the write
# (the close), said so; Esc then does not quit (a note), a second one does.
cp disk.img "$WORK/full.img"
# mdir prints the count in groups ("28 590 080 bytes free"): join them, and
# never run head with a count that is not a sane positive number.
free=$(mdir -i "$WORK/full.img" :: | awk '/bytes free/ { n = ""; for (i = 1; i <= NF && $i != "bytes"; i++) n = n $i; print n }')
case "$free" in ''|*[!0-9]*) free=0 ;; esac
if [ "$free" -gt 65536 ] && [ "$free" -lt 100000000 ]; then
    head -c $((free - 12288)) /dev/zero > "$WORK/filler"
    mcopy -i "$WORK/full.img" "$WORK/filler" ::FILLER.BIN
    rm -f "$WORK/filler"
else
    echo "  FAIL  could not read the disk's free space ('$free')"; fail=1
fi
DISK="$WORK/full.img" boot full "cook mincecraft.elf -flat -script -wait -world /FULL.WLD &" "WAIT:1" "countertop" "WAIT:5" \
          "TYPE:p" "WAIT:2" "KEY:esc" "WAIT:2" "TYPE:m" "WAIT:1" "KEY:esc" "WAIT:3"
python3 - "$WORK" <<'PY' || fail=1
import re, sys
work = sys.argv[1]
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
def log(n): return open(f'{work}/serial-{n}.log', 'rb').read().decode('latin-1')
SAVED = r'\[mincecraft\] saved (\S+) bytes=(\d+) hash=(\d+) x=(-?\d+) y=(-?\d+) z=(-?\d+)'
s1 = log('save')
check('[mincecraft] no world at /SAVE.WLD' in s1, 'save: no /SAVE.WLD yet, a new world')
saves = re.findall(SAVED, s1)
check(len(saves) == 2, 'save: P saved and Esc saved again: %d saves' % len(saves))
check(re.search(r'\[mincecraft\] inv 1=1\b', s1), 'save: a crust in hand')
s2 = log('load')
if saves:
    path, nbytes, h, x, y, z = saves[-1]
    check(int(nbytes) < 100000, 'save: %s bytes, well under 100 KB' % nbytes)
    m = re.search(r'\[mincecraft\] loaded /SAVE.WLD hash=(\d+) x=(-?\d+) y=(-?\d+) z=(-?\d+)', s2)
    check(m and m.groups() == (h, x, y, z), 'load: the last save back, hash and place: %r' % (m.groups() if m else None,))
    p = re.search(r'\[mincecraft\] p (-?\d+) (-?\d+) (-?\d+)', s2)
    check(p and (p.group(1), p.group(3)) == (x, z), 'load: the first tick where it was saved')
    check(re.search(r'\[mincecraft\] inv 1=1\b', s2), 'load: the crust in hand again')
c = log('check')
for f in ('BAD', 'SHORT', 'LONG'):
    check(f'[mincecraft] refused /{f}.WLD' in c, f'check: /{f}.WLD refused')
check('[mincecraft] refused /T.WLD' in c, 'the game refused /T.WLD (3 MB of zeros)')
check('refused /T.WLD' in c and re.search(r'\[mincecraft\] fps=\d+', c.split('refused /T.WLD')[-1]), 'and played on: fps after the refusal')
tsave = re.findall(SAVED, c)
a = log('again')
if tsave:
    m = re.search(r'\[mincecraft\] loaded /T.WLD hash=(\d+)', a)
    check(m and m.group(1) == tsave[-1][2], 'again: /T.WLD loads with the small save\'s hash: %r' % (m.group(1) if m else None,))
else:
    check(False, 'check: P saved over /T.WLD')
check('[mincecraft] no world at /mincecraft.wld' in a, 'again: no -world looks for HOME\'s /mincecraft.wld')
import os
check('[mincecraft] loaded /HIGH.WLD' in a, 'again: a save with the feet at y 64.3 (off the top block) loads')
cr = open(f'{work}/craft.out').read() if os.path.exists(f'{work}/craft.out') else ''
check('version 4 check ok True' in cr, 'saves are version 4, their CRC-32 right by zlib: %r' % cr.strip())
check('[mincecraft] loaded /V1.WLD' in c, 'a version 1 file (a byte sum, 11 counts) still loads')
check('[mincecraft] loaded /V2.WLD' in c, 'a version 2 file (a CRC, 11 counts) still loads')
check('[mincecraft] loaded /V3.WLD' in c, 'a version 3 file (14 counts, no hearts) still loads')
check('[mincecraft] refused /DEAD.WLD' in c, 'a save with no hearts left refused')
check('[mincecraft] refused /SWAP.WLD' in c, 'two runs of the grid swapped (a valid grid, the same sum) refused')
check('[mincecraft] HOME is too long' in a, 'a HOME of 151 characters refused, not cut')
import os
check(os.path.exists(f'{work}/t.bad') and open(f'{work}/t.bad', 'rb').read() == open(f'{work}/t.wld', 'rb').read(),
      'the refused /T.WLD was kept, whole, as /T.WLD.bad before the save over it')
check(not os.path.exists(f'{work}/bad-aside'), '-check leaves a refused file where it is (no /BAD.WLD.bad)')
fl = log('full')
check('[mincecraft] save failed' in fl and not re.search(SAVED, fl), 'full disk: P says the save failed, never "saved"')
first = fl.split('[mincecraft] mark')[0] if '[mincecraft] mark' in fl else fl
check('[mincecraft] bye' not in first, 'full disk: the first Esc did not quit (unsaved)')
check('[mincecraft] bye' in fl, 'full disk: the second Esc quit')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-save-test: PASS" || echo "mincecraft-save-test: FAIL"
exit "$fail"

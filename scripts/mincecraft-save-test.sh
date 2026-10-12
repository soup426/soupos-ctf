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
boot load "cook mincecraft.elf -script -wait -world /SAVE.WLD &" "WAIT:1" "countertop" "WAIT:5" "KEY:esc" "WAIT:4"
mcopy -n -i "$WORK/disk.img" ::SAVE.WLD "$WORK/good.wld" || { echo "  FAIL  no /SAVE.WLD on the disk"; fail=1; }
if [ -f "$WORK/good.wld" ]; then
    python3 -c "import sys;b=bytearray(open(sys.argv[1],'rb').read());b[100]^=1;open(sys.argv[2],'wb').write(b)" "$WORK/good.wld" "$WORK/bad.wld"
    head -c -10 "$WORK/good.wld" > "$WORK/short.wld"
    { cat "$WORK/good.wld"; printf 'JUNK'; } > "$WORK/long.wld"
    head -c 3000000 /dev/zero > "$WORK/t.wld"
    # (v0.60.202) the feet at y 64.3, a jump off the ceiling's top block: a
    # file play can write, so it must load (the header's y at offset 20,
    # the sum of every byte before the last four rewritten to match)
    python3 -c "
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
b[20:24] = struct.pack('<i', (64 << 16) + 21000)
b[-4:] = struct.pack('<I', sum(b[:-4]) & 0xFFFFFFFF)
open(sys.argv[2], 'wb').write(b)" "$WORK/good.wld" "$WORK/high.wld"
    for f in bad short long t high; do mcopy -o -i "$WORK/disk.img" "$WORK/$f.wld" "::${f^^}.WLD"; done
fi
boot check "cook mincecraft.elf -check -world /BAD.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /SHORT.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /LONG.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -flat -script -wait -world /T.WLD &" "WAIT:1" "countertop" "WAIT:5" \
           "TYPE:p" "WAIT:2" "KEY:esc" "WAIT:4"
mcopy -n -i "$WORK/disk.img" ::T.WLD.bad "$WORK/t.bad" 2>/dev/null
mdir -i "$WORK/disk.img" ::BAD.WLD.bad >/dev/null 2>&1 && echo yes > "$WORK/bad-aside"
boot again "cook mincecraft.elf -check -world /T.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /HIGH.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check" "UNTIL:@soupOS:"
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
check('[mincecraft] loaded /HIGH.WLD' in a, 'again: a save with the feet at y 64.3 (off the top block) loads')
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

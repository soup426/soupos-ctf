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
    qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
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
    for f in bad short long t; do mcopy -o -i "$WORK/disk.img" "$WORK/$f.wld" "::${f^^}.WLD"; done
fi
boot check "cook mincecraft.elf -check -world /BAD.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /SHORT.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check -world /LONG.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -flat -script -wait -world /T.WLD &" "WAIT:1" "countertop" "WAIT:5" \
           "TYPE:p" "WAIT:2" "KEY:esc" "WAIT:4"
boot again "cook mincecraft.elf -check -world /T.WLD" "UNTIL:@soupOS:" \
           "cook mincecraft.elf -check" "UNTIL:@soupOS:"
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
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "mincecraft-save-test: PASS" || echo "mincecraft-save-test: FAIL"
exit "$fail"

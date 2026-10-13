#!/usr/bin/env bash
# doom-wake-test.sh - a monster waits its reaction time before it fires
# (v0.60.316, review R-D46). The attack cooldown started at 0 and the attack
# ran in the same step as the wake, so every monster that saw the player
# fired on the first tic: in E1M1's nukage room (doom at 3216 -3184 270)
# health was 10 a third of a second in, the player dead in 1.5 s with no key
# pressed. Doom (p_enemy.c): reactiontime 8, counted down a chase step at a
# time (a zombieman's is 4 tics, an imp's or a sergeant's 3), no missile
# until it is 0; then P_CheckMissileRange's chance by distance. Here, from
# "[doom] hurt N by WHAT at tic T": no hit before tic 22 (Doom's soonest,
# 7 chase steps of 3 and the waking tic), so health is 100 a third of a
# second in. At least one hit is required (v0.60.322, review R-T47): with
# the fix reverted there are no hurt lines and both checks passed on an
# empty list; the player dies here in 3 s. And the range chance holds a
# shot (R-D58): each shotgun guy fires once before the player dies (the
# cooldown is 50 tics), all four on tic 24 without missile_hold, so their
# hits fall on more than one tic.
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }
fail=0
WORK=$(mktemp -d /tmp/soupos-doomwake.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill "$PID" 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:2" "doom at 3216 -3184 270" "WAIT:6" \
    "KEY:ret" "WAIT:1" "KEY:ret" "WAIT:1" "KEY:ret" "UNTIL:[doom] E1M1 up" "WAIT:3" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
t = open(sys.argv[1], errors='replace').read().replace('\r', '')
hits = [(int(n), w, int(tic), int(h)) for n, w, tic, h in re.findall(r'\[doom\] hurt (\d+) by ([a-z ]+) at tic (\d+), health (-?\d+)', t)]
first = min((h[2] for h in hits), default=None)
ok1 = first is not None and first >= 22
print(('  ok    ' if ok1 else '  FAIL  ') + f'the first hit came at tic {first} (Doom: not before reactiontime 8 chase steps, tic 22 at the soonest; none means no hurt lines); {len(hits)} hits in 3 s')
early = [h for h in hits if h[2] < 22]
if early: print('        before tic 22: ' + ', '.join(f'{n} by {w} at {tic}' for n, w, tic, _ in early[:8]))
sg = sorted({h[2] for h in hits if h[1] == 'shotgun guy'})
held = len(sg) > 1
print(('  ok    ' if held else '  FAIL  ') + f'a shotgun guy holds a shot (P_CheckMissileRange): their hits at tics {sg}')
# Full health a third of a second in (12 tics), as the review measured it.
# (Dead within 3 s here still: every monster spawns whatever the skill and
# the hitscans' damage is fixed, R-D2 and R-D30.)
full = bool(hits) and all(h[2] > 12 for h in hits)
print(('  ok    ' if full else '  FAIL  ') + 'health is 100 a third of a second in (tic 12)')
sys.exit(0 if ok1 and held and full else 1)
PY
[ "$fail" = 0 ] && echo "doom-wake-test: PASS" || echo "doom-wake-test: FAIL"
exit "$fail"

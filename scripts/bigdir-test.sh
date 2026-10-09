#!/usr/bin/env bash
# bigdir-test.sh - a bowl of 302 entries, more than fat_ls's FAT_LS_MAX of
# 128 at a time (v0.60.125): peek.elf (SYS_READDIR), a glob, serve's count,
# forage.elf, portions' bytes and Tab all reach every one, as mtools sees
# them; the last two made (one with a long name) sit far past 128.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bigdir.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H/f"
for i in $(seq 300); do echo "file $i" > "$H/f/f$i.txt"; done
mmd -i "$WORK/disk.img" ::/big
mcopy -i "$WORK/disk.img" "$H"/f/* ::/big/
echo "the long one" > "$H/A long name past the end.txt"; echo "last" > "$H/zzlast.txt"
mcopy -i "$WORK/disk.img" "$H/A long name past the end.txt" "$H/zzlast.txt" ::/big/
keys=("headchef" "rosemary" "WAIT:1"
  'cook peek.elf /big > /peek.txt' "UNTIL:@soupOS:"
  'n=0 ; for f in /big/* ; do n=$((n+1)) ; done ; slurp G:$n' "UNTIL:@soupOS:"
  'for f in /big/* ; do slurp "$f" ; done > /glob.txt' "UNTIL:@soupOS:"
  'cd /big ; serve > /serve.txt ; cd /' "UNTIL:@soupOS:"
  'cook forage.elf /big | cook weigh.elf -l > /forage.txt' "UNTIL:@soupOS:"
  'portions /big > /portions.txt' "UNTIL:@soupOS:"
  'TYPE:pour /big/zzl' "KEY:tab" "KEY:ret" "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
for f in peek glob serve forage portions; do mcopy -n -i "$WORK/disk.img" "::/$f.txt" "$WORK/$f.txt" 2>/dev/null || touch "$WORK/$f.txt"; done
bad=0
# what mtools lists: the long names where there are, else the 8.3 one
mdir -i "$WORK/disk.img" -b ::/big | sed 's|^::/big/||' | sort > "$WORK/m.names"
nm=$(wc -l < "$WORK/m.names")
[ "$nm" = 302 ] || { echo "        mtools sees $nm entries, not 302"; bad=1; }
np=$(grep -c . "$WORK/peek.txt")
for n in zzlast.txt 'A long name past the end.txt' f1.txt f300.txt; do grep -F -- "$n" "$WORK/peek.txt" >/dev/null || { echo "        peek.elf does not list '$n'"; bad=1; }; done
[ "$np" -ge 302 ] || { echo "        peek.elf printed $np lines, fewer than 302 entries"; bad=1; }
grep -x 'G:302' "$WORK/log" >/dev/null || { echo "        the glob gave $(grep '^G:' "$WORK/log")"; bad=1; }
# the glob's order, a name a line, against bash's (byte order) over the same names
mkdir -p "$H/all"; cp "$H"/f/* "$H/A long name past the end.txt" "$H/zzlast.txt" "$H/all/"
(cd "$H/all" && LC_ALL=C bash -c 'for f in *; do echo "/big/$f"; done') > "$WORK/want.glob"
cmp -s "$WORK/want.glob" "$WORK/glob.txt" || { echo "        the glob's order differs from bash's"; diff "$WORK/want.glob" "$WORK/glob.txt" | head -6 | sed 's/^/          /'; bad=1; }
grep -E 'Serving /big +- +302 items' "$WORK/serve.txt" >/dev/null || { echo "        serve says: $(grep Serving "$WORK/serve.txt")"; bad=1; }
[ "$(grep -c 'B$' "$WORK/serve.txt")" = 302 ] || { echo "        serve listed $(grep -c 'B$' "$WORK/serve.txt") files"; bad=1; }
[ "$(tr -d ' ' < "$WORK/forage.txt")" = 303 ] || { echo "        forage.elf found $(cat "$WORK/forage.txt") (the bowl and 302)"; bad=1; }
bytes=$(cat "$H"/all/* | wc -c)
grep -E " $bytes +total for /big" "$WORK/portions.txt" >/dev/null || { echo "        portions: $(tail -1 "$WORK/portions.txt"), not $bytes bytes"; bad=1; }
grep -x 'last' "$WORK/log" >/dev/null || { echo "        Tab did not finish /big/zzlast.txt"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    a bowl of 302 is reached whole: peek.elf, a glob (in bash's order), serve, forage.elf, portions ($bytes bytes), Tab to the last made" \
    || { echo "  FAIL  some of a 302-entry bowl is out of reach"; fail=1; }
exit $fail

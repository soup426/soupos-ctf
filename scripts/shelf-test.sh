#!/usr/bin/env bash
# shelf-test.sh - shelve, unshelve and shelf against bash's pushd, popd and
# dirs (v0.60.99), through files (a builtin's > F), bash's paths made
# soupOS's.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-shelf.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'cd / ; mkbowl /s1 ; mkbowl /s2 ; mkbowl /s3'
  'shelve /s1 > /d1.txt'
  'shelve /s2 >> /d1.txt'
  'shelve /s3 >> /d1.txt'
  'shelf >> /d1.txt'
  'shelve >> /d1.txt'
  'unshelve >> /d1.txt'
  'slurp $PWD >> /d1.txt'
  'unshelve >> /d1.txt ; unshelve >> /d1.txt'
  'unshelve ; slurp D2:$?'
  'shelve /nope ; slurp D3:$?'
  'shelve /s1 > /d2.txt ; shelf -c ; shelf >> /d2.txt'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
B="$WORK/b"; mkdir -p "$B"
script=""
for l in "${LINES[@]}"; do
    b=${l//unshelve/popd}; b=${b//shelve/pushd}; b=${b//shelf/dirs}; b=${b//slurp/echo}; b=${b//mkbowl/mkdir}
    b=${b//\/s/$B\/s}; b=${b//\/d/$B\/d}; b=${b//\/nope/$B\/nope}; b=${b//cd \//cd $B}
    script+="$b"$'\n'
done
env -i PATH="$PATH" HOME=/nonexistent bash -c "$script" 2>/dev/null | grep -E '^D[0-9]:' > "$WORK/want"
tr -d '\r' < "$WORK/serial.log" | grep -E '^D[0-9]:' > "$WORK/got"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { echo "        statuses:"; diff "$WORK/want" "$WORK/got" | sed 's/^/          /'; bad=1; }
for k in 1 2; do
    mcopy -n -i "$WORK/disk.img" "::/d$k.txt" "$WORK/got$k" 2>/dev/null || { echo "        d$k.txt was not written"; bad=1; continue; }
    sed -e "s#$B/#/#g" -e "s#$B\\b#/#g" "$B/d$k.txt" > "$WORK/want$k"
    cmp -s "$WORK/got$k" "$WORK/want$k" || { echo "        d$k.txt differs:"; diff "$WORK/want$k" "$WORK/got$k" | sed 's/^/          /'; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    the shelf is bash's directory stack (two files and two statuses: push three, list, swap, pop to the root, empty, a bowl not there, -c)" \
    || { echo "  FAIL  the shelf differs from bash's pushd/popd/dirs"; fail=1; }
exit $fail

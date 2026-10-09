#!/usr/bin/env bash
# colon-test.sh - :, fresh and spoiled (sh's :, true and false) against
# bash (v0.60.104): statuses, : ${x:=5} setting x, : > F emptying F, in
# while and until, with && and ||.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-colon.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  ': ; slurp C1:$? ; fresh ; slurp C2:$? ; spoiled ; slurp C3:$?'
  ': ${x:=5} ; slurp C4:$x'
  'slurp full > /e.txt ; : > /e.txt'
  'n=0 ; while : ; do n=$((n+1)) ; [ $n = 3 ] && break ; done ; slurp C5:$n'
  'until fresh ; do slurp no ; done ; slurp C6:$?'
  'spoiled || slurp C7:or ; fresh && slurp C8:and ; ! spoiled ; slurp C9:$?'
  ': these words do nothing ; slurp C10:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/b"
tr -d '\r' < "$WORK/serial.log" | grep -E '^C[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//fresh/true}; b=${b//spoiled/false}; b=${b//\/e.txt/$WORK\/b\/e.txt}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^C[0-9]+:' > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
mcopy -n -i "$WORK/disk.img" ::/e.txt "$WORK/got.e" 2>/dev/null || { echo "        e.txt is gone"; bad=1; }
[ -f "$WORK/got.e" ] && [ ! -s "$WORK/got.e" ] || { echo "        : > F did not empty F"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    :, fresh and spoiled are bash's :, true and false ($(wc -l < "$WORK/want") lines: statuses, : \${x:=5}, : > F empty, while :, until fresh, && || !)" \
    || { echo "  FAIL  :, fresh or spoiled differ from bash"; fail=1; }
exit $fail

#!/usr/bin/env bash
# longword-test.sh - long word lists, and nothing run cut short (v0.60.30).
#
# A for loop over $(cook tally.elf 6000) runs 6000 times, as bash's over
# $(seq 6000) does (it ran 64); a list too long for even the bigger buffer
# says it was cut; a plain command too long once expanded is refused rather
# than run cut short.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-longword.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'n=0 ; for i in $(cook tally.elf 6000) ; do n=$((n+1)) ; done ; slurp W:n=$n'
  'for i in $(cook tally.elf 6000) ; do last=$i ; done ; slurp W:last=$last'
  'slurp W:short $(cook tally.elf 5)'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("for i in \$(cook tally.elf 9000) ; do x=1 ; done ; slurp W2:after-cut" "UNTIL:@soupOS:"
       "slurp W2:\$(cook tally.elf 200)" "UNTIL:@soupOS:" "slurp W2:status=\$?" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
grep -E '^W:' "$WORK/out.log" > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//cook tally.elf/seq}; script+="$b"$'\n'; done
bash -c "$script" | grep -E '^W:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    a for loop over 6000 words runs 6000 times, as bash's does ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  long word lists differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
grep -E "for: the word list was cut \([0-9]+ bytes, [0-9]+ words at most\); [0-9]+ words kept" "$WORK/out.log" >/dev/null && grep -qx 'W2:after-cut' "$WORK/out.log" \
    && echo "  ok    a list too long even now says it was cut, and the line goes on" \
    || { echo "  FAIL  a cut word list was not reported"; fail=1; }
grep -q "The command is too long once expanded" "$WORK/out.log" && ! grep -q '^W2:1 2 3' "$WORK/out.log" && grep -qx 'W2:status=1' "$WORK/out.log" \
    && echo "  ok    a command too long once expanded is refused, status 1, not run cut short" \
    || { echo "  FAIL  a too-long command was run or not refused"; fail=1; }
exit $fail

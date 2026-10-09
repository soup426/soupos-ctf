#!/usr/bin/env bash
# layer-test.sh - layer.elf against the host's paste, byte for byte (v0.60.16;
# - more than once taking turns, and -s, v0.60.147).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-layer.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
export LC_ALL=C
cp disk.img "$WORK/disk.img"
seq 1 5 > "$WORK/N.TXT"                                            # 5 lines
printf 'salt\npepper\nthyme' > "$WORK/H.TXT"                      # 3, no final newline
: > "$WORK/E.TXT"                                                  # empty
printf '\n\n\n\n\n\n\n' > "$WORK/B.TXT"                        # 7 empty lines
seq 1 3000 | sed 's/$/ a longer line to cross a read/' > "$WORK/BIG.TXT"
for f in N H E B BIG; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#layer.elf /N.TXT /H.TXT#paste N.TXT H.TXT"
  "2#layer.elf /H.TXT /N.TXT /E.TXT#paste H.TXT N.TXT E.TXT"
  "3#layer.elf /E.TXT /E.TXT#paste E.TXT E.TXT"
  "4#layer.elf -d ',;' /N.TXT /N.TXT /H.TXT /N.TXT#paste -d ',;' N.TXT N.TXT H.TXT N.TXT"
  "5#layer.elf -d '\\0' /N.TXT /H.TXT#paste -d '\\0' N.TXT H.TXT"
  "6#layer.elf -d '\\t\\\\' /H.TXT /N.TXT /H.TXT#paste -d '\\t\\\\' H.TXT N.TXT H.TXT"
  "7#layer.elf /B.TXT /H.TXT#paste B.TXT H.TXT"
  "8#spoon.elf /H.TXT | layer.elf /N.TXT - /N.TXT#paste N.TXT - N.TXT < H.TXT"
  "9#layer.elf /BIG.TXT /N.TXT /BIG.TXT#paste BIG.TXT N.TXT BIG.TXT"
  "10#layer.elf -d: /N.TXT /N.TXT#paste -d: N.TXT N.TXT"
  "11#spoon.elf /N.TXT | layer.elf - -#paste - - < N.TXT"
  "12#spoon.elf /N.TXT | layer.elf - /H.TXT -#paste - H.TXT - < N.TXT"
  "13#spoon.elf /BIG.TXT | layer.elf - - -#paste - - - < BIG.TXT"
  "14#layer.elf -s /N.TXT /H.TXT /E.TXT /B.TXT#paste -s N.TXT H.TXT E.TXT B.TXT"
  "15#layer.elf -s -d ',;' /N.TXT /H.TXT#paste -s -d ',;' N.TXT H.TXT"
  "16#layer.elf -sd: /N.TXT#paste -sd: N.TXT"
  "17#spoon.elf /N.TXT | layer.elf -s - /H.TXT -#paste -s - H.TXT - < N.TXT"
  "18#layer.elf -s /BIG.TXT /N.TXT#paste -s BIG.TXT N.TXT"
  "19#layer.elf -d '\\0' -s /N.TXT#paste -d '\\0' -s N.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
keys+=("cook layer.elf /N.TXT /NOPE.TXT" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/H$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && bash -c "$host" ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "        differs: $soup  (host: $host)"; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's paste byte for byte (uneven lengths, empty files, -d lists and escapes, stdin as -, - more than once, -s, past a read)" \
    || { echo "  FAIL  layer differs from the host's paste"; fail=1; }
if grep -q 'layer: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'layer: cannot open' for a missing file"; fail=1; fi
exit $fail

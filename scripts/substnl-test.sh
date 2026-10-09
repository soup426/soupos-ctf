#!/usr/bin/env bash
# substnl-test.sh - "$(cmd)" and x=$(cmd) keep cmd's inner newlines, as
# bash's do; unquoted $(cmd) still splits (v0.60.109). Only the trailing
# newlines go.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-substnl.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'slurp "N1:$(cook stalk.elf a/b c/d)"'
  'slurp "N2:$(cook stalk.elf a/b c/d e/f)"'
  'slurp N3:$(cook stalk.elf a/b c/d)'
  'x=$(cook stalk.elf a/b c/d) ; slurp "N4:$x"'
  'slurp "N5:[$(cook stalk.elf a/b c/d)] after"'
  'y="pre $(cook stalk.elf a/b c/d) post" ; slurp "N6:$y"'
  'slurp "N7:$(cook stalk.elf a/b c/d)" | cook weigh.elf -l'
  'z=$(cook stalk.elf a/b c/d) ; slurp N8:$z'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
# what each line prints, from the N tag up to the next prompt (the weigh
# count is a bare number)
tr -d '\r' < "$WORK/serial.log" | awk '/^N[0-9]+:|^ *[0-9]+$/{p=1} /@soupOS:/{p=0} p' | grep -vE 'WCOUT|\[/|\[proc ' | sed 's/^ *//' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook stalk.elf/dirname}; b=${b//cook weigh.elf -l/wc -l}; b=${b//slurp/echo}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | sed 's/^ *//' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    \"\$(cmd)\" and x=\$(cmd) keep cmd's newlines, bare \$(cmd) splits, as bash ($(wc -l < "$WORK/want") lines)"
else echo "  FAIL  \$(cmd)'s newlines differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

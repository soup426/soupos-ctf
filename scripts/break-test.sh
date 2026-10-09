#!/usr/bin/env bash
# break-test.sh - break and continue against bash (v0.60.44).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-break.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'for i in 1 2 3 4 ; do slurp K1:$i ; case $i in 2) break ;; esac ; done ; slurp K2:after'
  'for i in 1 2 3 4 ; do case $i in 2) continue ;; esac ; slurp K3:$i ; done'
  'n=0 ; while cook taste.elf $n -lt 10 ; do n=$((n+1)) ; if cook taste.elf $n -eq 3 ; then break ; fi ; done ; slurp K4:$n'
  'for a in x y ; do for b in 1 2 3 ; do slurp K5:$a$b ; if cook taste.elf $b = 2 ; then break 2 ; fi ; done ; done ; slurp K6:end'
  'for a in x y ; do for b in 1 2 3 ; do if cook taste.elf $b = 2 ; then continue 2 ; fi ; slurp K7:$a$b ; done ; slurp K8:never ; done'
  'n=0 ; until cook taste.elf $n -ge 4 ; do n=$((n+1)) ; if cook taste.elf $n = 2 ; then continue ; fi ; slurp K9:$n ; done'
  'break ; slurp K10:$?'
  'for i in 1 2 ; do break 5 ; done ; slurp K11:ok'
  'for i in a b c d ; do slurp K12:$i ; if cook taste.elf $i = b ; then break ; fi ; done | stack.elf'
  'while slurp K13:tick ; do break ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^K[0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do b=${l//cook taste.elf/test}; b=${b//stack.elf/tac}; b=${b//slurp/echo}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^K[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: break and continue in case and if, while, until, 2 for nested loops, outside any loop, more than are open, a piped loop)"
else echo "  FAIL  break/continue differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
tr -d '\r' < "$WORK/serial.log" | grep -F "break: only meaningful in a for, while or until loop" >/dev/null \
    && echo "  ok    break outside a loop says so" || { echo "  FAIL  break outside a loop was silent"; fail=1; }
exit $fail

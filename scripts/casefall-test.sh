#!/usr/bin/env bash
# casefall-test.sh - case's ;& and ;;& against bash (v0.60.97).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-casefall.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf '%s\n' 'case b in' '  a) echo C7:a ;;' '  b) echo C7:b' '     ;&' '  c) echo C7:c' '     ;;&' '  *) echo C7:any ;;' 'esac' > "$WORK/c.sh"
sed -e 's/\becho\b/slurp/g' "$WORK/c.sh" > "$WORK/C.SH"; mcopy -i "$WORK/disk.img" "$WORK/C.SH" ::/c.sh
LINES=(
  'case a in a) slurp C1:a ;& b) slurp C1:b ;& c) slurp C1:c ;; d) slurp C1:d ;; esac'
  'case b in a) slurp C2:a ;;& b) slurp C2:b ;;& *) slurp C2:star ;; esac'
  'case x in a) slurp no ;& x) slurp C3:x ;& y) slurp C3:y ;; esac'
  'case q in q) slurp C4:q ;;& [a-z]) slurp C4:letter ;;& [0-9]) slurp C4:digit ;; esac'
  'case z in z) slurp C5:z ;& esac ; slurp C5:after'
  'for v in 1 2 ; do case $v in 1) slurp C6:one ;& 2) slurp C6:two ;; esac ; done'
  'follow /c.sh'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^C[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//follow \/c.sh/bash $WORK\/c.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^C[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    ;& and ;;& do what bash's do ($(wc -l < "$WORK/want") lines: ;& down a run of arms, ;;& testing on, entering partway, ;& before esac, in a loop, a script over lines)"
else echo "  FAIL  case fall-through differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

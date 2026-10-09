#!/usr/bin/env bash
# loopredir-test.sh - done > F, done >> F, done < F (and fi, esac) against bash (v0.60.61).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-loopredir.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'for i in 1 2 3 ; do slurp line$i ; done > /o1.txt ; slurp R1:$?'
  'n=0 ; while (( n < 2 )) ; do slurp w$n ; n=$((n+1)) ; done >> /o1.txt'
  'if cook taste.elf 1 -eq 1 ; then slurp yes ; else slurp no ; fi > /o2.txt'
  'case b in a) slurp A ;; b) slurp B ;; esac > /o3.txt'
  'f=/o4.txt ; for i in 1 ; do cook call.elf from a program ; slurp then a builtin ; done > $f'
  'while take l ; do slurp R2:[$l] ; done < /o1.txt ; slurp R3:$?'
  'while take l ; do slurp R4:$l ; done < /nope.txt ; slurp R5:$?'
  'for i in 1 2 ; do cook taste.elf 1 -eq 2 ; done > /o5.txt ; slurp R6:$?'
  'for i in 1 2 ; do for j in a b ; do slurp $i$j ; done > /o6.txt ; done ; slurp R7:$?'
  'g=/o1.txt ; while take a b ; do slurp R8:$b ; done < $g'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('for i in 1 ; do slurp x ; done 2> /o9.txt ; slurp R9:$?' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/s" "$WORK/b"
for k in 1 2 3 4 5 6; do mcopy -n -i "$WORK/disk.img" "::/o$k.txt" "$WORK/s/o$k.txt" 2>/dev/null || echo "(missing)" > "$WORK/s/o$k.txt"; done
tr -d '\r' < "$WORK/serial.log" | grep -E '^R[0-8]:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook taste.elf/test}; b=${b//cook call.elf/echo}; b=${b//slurp/echo}; b=${b//take/read}; b=${b//\/o/$WORK\/b\/o}; b=${b//\/nope/$WORK\/b\/nope}
    script+="$b"$'\n'
done
bash -c "$script" 2>/dev/null | grep -E '^R[0-8]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    statuses and what < feeds the loop are bash's ($(wc -l < "$WORK/want") lines)"
else echo "  FAIL  printed lines differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
same=0
for k in 1 2 3 4 5 6; do
    if cmp -s "$WORK/s/o$k.txt" "$WORK/b/o$k.txt"; then same=$((same + 1))
    else echo "  FAIL  o$k.txt differs from bash's:"; diff "$WORK/b/o$k.txt" "$WORK/s/o$k.txt" | sed 's/^/        /'; fail=1; fi
done
[ "$same" = 6 ] && echo "  ok    all six files are bash's, byte for byte (>, >> after it, fi, esac, a program in the loop, \$f, a failing body, a loop inside a loop)"
if tr -d '\r' < "$WORK/serial.log" | grep -F 'Only `| program`, `> file`' >/dev/null; then
    echo "  ok    done 2> F is refused out loud, not run half right"
else echo "  FAIL  done 2> F was not refused"; fail=1; fi
exit $fail

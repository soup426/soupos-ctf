#!/usr/bin/env bash
# pipeloop-test.sh - a whole loop into a program, against bash (v0.60.37).
#
# for, while, if and case with `| PROG` after their done, fi or esac; each
# line writes a file in soupOS and in bash (slurp as echo, the programs as
# their host tools), and the files must match. Anything else after done is
# refused rather than ignored (> F, >> F and < F are allowed since v0.60.61,
# loopredir-test.sh has them; 2> F is still refused).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-pipeloop.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
printf 'pear\napple\nfig\n' > "$WORK/host/F.TXT"
mcopy -i "$WORK/disk.img" "$WORK/host/F.TXT" ::/F.TXT
LINES=(
  'for x in c a b ; do slurp $x ; done | rack.elf > /R1.TXT'
  'while slurp y ; do x=1 ; done | skim.elf -n 3 > /R2.TXT'
  'n=0 ; while cook taste.elf $n -lt 5 ; do slurp line$n ; n=$((n+1)) ; done | stack.elf > /R3.TXT'
  'if cook taste.elf 1 = 1 ; then slurp yes ; else slurp no ; fi | raise.elf > /R4.TXT'
  'case b in a) slurp A ;; b) slurp B ; slurp B2 ;; esac | rack.elf -r > /R5.TXT'
  'cook spoon.elf /F.TXT | while take l ; do slurp "got:$l" ; done | rack.elf > /R6.TXT'
  'for a in 1 2 ; do for b in x y ; do slurp $a$b ; done | stack.elf ; done | raise.elf > /R7.TXT'
  'for x in a ; do slurp $x ; done | sift.elf zzz > /R8.TXT ; cook call.elf $? > /S8.TXT'
)
FILES="R1 R2 R3 R4 R5 R6 R7 R8 S8"
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("for x in a ; do slurp \$x ; done 2> /J.TXT" "UNTIL:@soupOS:" "cook mise.elf after-loops" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
script=""
for l in "${LINES[@]}"; do
    b=${l//cook spoon.elf \//cat }; b=${b//cook taste.elf/test}; b=${b//cook call.elf/echo}; b=${b//slurp/echo}
    b=${b//take/read}; b=${b//rack.elf/sort}; b=${b//skim.elf/head}; b=${b//stack.elf/tac}
    b=${b//raise.elf/tr a-z A-Z}; b=${b//sift.elf/grep}; b=${b//> \//> }
    script+="$b"$'\n'
done
( cd "$WORK/host" && LC_ALL=C bash -c "$script" ) >/dev/null 2>&1
bad=0
for f in $FILES; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" || { echo "        $f.TXT: got '$(tr '\n' ' ' < "$WORK/got.$f")', bash '$(tr '\n' ' ' < "$WORK/host/$f.TXT")'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#LINES[@]} lines write what bash writes ($(echo $FILES | wc -w) files: for, an endless while, while with arithmetic, if, case, a program into a loop into a program, a loop in a loop, the status)" \
    || { echo "  FAIL  a piped loop differs from bash"; fail=1; }
tr -d '\r' < "$WORK/serial.log" | grep -F 'Only `| program`, `> file`, `>> file` or `< file` may follow done, fi or esac' >/dev/null && ! mdir -b -i "$WORK/disk.img" ::/J.TXT >/dev/null 2>&1 \
    && echo "  ok    done 2> F is refused, not quietly ignored" || { echo "  FAIL  done 2> F was not refused"; fail=1; }
grep -q 'TESTOUT args=after-loops' "$WORK/serial.log" && echo "  ok    programs still run after all that" \
    || { echo "  FAIL  a program did not run after the loops"; fail=1; }
exit $fail

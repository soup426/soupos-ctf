#!/usr/bin/env bash
# bpipe-test.sh - a builtin or a function into a program, against bash (v0.60.27).
#
# Each line runs in soupOS and in bash (slurp as echo, pour as cat, the
# programs as their host tools) and the file it writes must match.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bpipe.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT "$WORK/host/HELLO.TXT"
seq 1 6000 | sed 's/$/ a line long enough to fill pipes/' > "$WORK/host/BIG.TXT"   # ~200 KB
head -c 4096 /dev/urandom > "$WORK/host/RAND.BIN"                                   # every byte value
for f in BIG.TXT RAND.BIN; do mcopy -i "$WORK/disk.img" "$WORK/host/$f" "::/$f"; done
LINES=(
  'slurp hello world | raise.elf > /P1.TXT'
  'two() { slurp one ; slurp two ; } ; two | stack.elf > /P2.TXT'
  'slurp b a c | swap.elf " " "\n" | rack.elf > /P3.TXT'
  'many() { for a in 1 2 3 4 5 6 ; do for b in 0 1 2 3 4 5 6 7 8 9 ; do for c in 0 1 2 3 4 5 6 7 8 9 ; do for d in 0 1 2 3 4 5 6 7 8 9 ; do slurp line $a$b$c$d ; done ; done ; done ; done ; } ; many | dregs.elf -n 2 > /P4.TXT'
  'many | weigh.elf -l > /P5.TXT'
  'many | skim.elf -n 1 > /P6.TXT'
  'slurp hi | sift.elf nope > /P7.TXT ; cook call.elf $? > /P8.TXT'
  "slurp 'a|b' | raise.elf > /P9.TXT"
  'slurp ignored | raise.elf < /HELLO.TXT > /P10.TXT'
  'pour /HELLO.TXT | raise.elf > /P11.TXT'
  'pour /BIG.TXT | weigh.elf -l > /P12.TXT'
  'pour /RAND.BIN | mince.elf > /P13.TXT'
  'forever() { while slurp yes ; do x=1 ; done ; } ; forever | skim.elf -n 2 > /P15.TXT'
)
FILES="P1 P2 P3 P4 P5 P6 P7 P8 P9 P10 P11 P12 P13 P15"
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("cook mise.elf after-pipes" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
script=""
for l in "${LINES[@]}"; do
    b=${l//slurp/echo}; b=${b//pour /cat }; b=${b//cook tally.elf/seq}; b=${b//cook call.elf/echo}
    b=${b//raise.elf/tr a-z A-Z}; b=${b//stack.elf/tac}; b=${b//swap.elf/tr}; b=${b//rack.elf/sort}
    b=${b//dregs.elf/tail}; b=${b//weigh.elf -l/wc -l}; b=${b//skim.elf/head}; b=${b//sift.elf/grep}
    b=${b//mince.elf/hexdump -C}
    b=${b//\//}                        # /X.TXT -> X.TXT
    script+="$b"$'\n'
done
( cd "$WORK/host" && LC_ALL=C bash -c "$script" ) >/dev/null 2>&1
bad=0
for f in $FILES; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    # weigh pads like wc only when given a file: compare -l's number alone
    if [ "$f" = P5 ] || [ "$f" = P12 ]; then a=$(tr -d ' ' < "$WORK/got.$f"); b=$(tr -d ' ' < "$WORK/host/$f.TXT"); [ "$a" = "$b" ] && continue
    else cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" && continue; fi
    echo "        $f.TXT: got '$(head -c 120 "$WORK/got.$f")', bash '$(head -c 120 "$WORK/host/$f.TXT")'"; bad=1
done
[ "$bad" = 0 ] && echo "  ok    all ${#LINES[@]} lines write what bash writes ($(echo $FILES | wc -w) files: a builtin, a function, three stages, 6000 lines (60 KB) from a function, a reader that stops early, the status, a quoted |, a < that wins, pour as cat: exact, 200 KB, every byte value, a left side that never ends)" \
    || { echo "  FAIL  a builtin's pipe differs from bash"; fail=1; }
grep -q 'TESTOUT args=after-pipes' "$WORK/serial.log" && echo "  ok    programs still run after all that" \
    || { echo "  FAIL  a program did not run after the pipe lines"; fail=1; }
exit $fail

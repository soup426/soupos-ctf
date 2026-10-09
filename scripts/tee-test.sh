#!/usr/bin/env bash
# tee-test.sh - divvy.elf against the host's tee (v0.56.7).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tee.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
seq 1 300 | sed 's/^/a line of soup /' > "$WORK/T.TXT"
printf 'second part\nwith no final newline' > "$WORK/T2.TXT"
for f in T T2; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook spoon.elf /T.TXT | divvy.elf /T1.TXT > /O1.TXT" "WAIT:2" \
    "cook spoon.elf /T2.TXT | divvy.elf -a /T1.TXT > /O2.TXT" "WAIT:2" \
    "cook spoon.elf /T.TXT | divvy.elf /A.TXT /B.TXT > /O3.TXT" "WAIT:2" \
    "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "cook spoon.elf /T2.TXT | divvy.elf /etc/X.TXT /home/cook/M.TXT > /home/cook/O4.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
get() { mcopy -n -i "$WORK/disk.img" "::$1" "$WORK/got_$2" 2>/dev/null || : > "$WORK/got_$2"; }
for f in O1 O2 O3 T1 A B; do get "/$f.TXT" "$f"; done
get /home/cook/O4.TXT O4; get /home/cook/M.TXT M
# The host's tee, the same steps.
( cd "$WORK" && mkdir -p host && cd host && tee T1.TXT < ../T.TXT > O1.TXT && tee -a T1.TXT < ../T2.TXT > O2.TXT \
  && tee A.TXT B.TXT < ../T.TXT > O3.TXT )
bad=0
for f in O1 O2 O3 T1 A B; do cmp -s "$WORK/got_$f" "$WORK/host/$f.TXT" || { echo "        differs: $f.TXT"; bad=1; }; done
[ "$bad" = 0 ] && echo "  ok    stdout and the files match the host's tee: one file, -a appending, two files" \
    || { echo "  FAIL  divvy.elf differs from the host's tee"; fail=1; }
L=$(tr -d '\r' < "$WORK/serial.log")
cmp -s "$WORK/got_O4" "$WORK/T2.TXT" && cmp -s "$WORK/got_M" "$WORK/T2.TXT" \
    && ! mdir -b -i "$WORK/disk.img" ::/etc 2>/dev/null | grep -i "X.TXT" >/dev/null \
    && echo "$L" | grep "/divvy.elf: open /etc/X.TXT for writing refused" >/dev/null \
    && echo "$L" | grep -E "^\[proc [0-9]+\] /divvy.elf exited with code 1" >/dev/null \
    && echo "  ok    the cook cannot tee into /etc (refused, logged, exit 1), and the rest still flows" \
    || { echo "  FAIL  tee's permissions or carrying on"; fail=1; }
exit $fail

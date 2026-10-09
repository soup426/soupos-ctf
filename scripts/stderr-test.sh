#!/usr/bin/env bash
# stderr-test.sh - 2> 2>> and 2>&1 against bash (v0.60.21).
#
# pair.elf is cmp under a kitchen name and writes its EOF and missing-file
# messages to stderr, as cmp does. Each line runs in soupOS and in bash (cmp
# for pair, tr a-z A-Z for raise, wc for weigh, echo for call) and every file
# either side writes must match byte for byte, "cmp:" read as "pair:".
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-stderr.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
printf 'a\nb\n' > "$WORK/host/A.TXT"; printf 'a\n' > "$WORK/host/B.TXT"; printf 'x\nb\n' > "$WORK/host/C.TXT"
for f in A B C; do mcopy -i "$WORK/disk.img" "$WORK/host/$f.TXT" "::/$f.TXT"; done
LINES=(
  'cook pair.elf A.TXT B.TXT 2> E1.TXT'
  'cook pair.elf A.TXT B.TXT > O2.TXT 2>&1'
  'cook pair.elf A.TXT B.TXT 2> E3.TXT'
  'cook pair.elf A.TXT NOPE.TXT 2>> E3.TXT'
  'cook pair.elf A.TXT C.TXT > O4.TXT 2> E4.TXT'
  'cook pair.elf A.TXT B.TXT 2>&1 | raise.elf > O5.TXT'
  'cook pair.elf A.TXT C.TXT 2> E6.TXT | skim.elf > O6.TXT'
  'cook pair.elf A.TXT B.TXT 2> E7.TXT ; cook call.elf $? > S7.TXT'
  'cook call.elf a2> W8.TXT'
  'cook pair.elf A.TXT C.TXT 2>&1 > O9.TXT'
  'cook pair.elf A.TXT B.TXT 2>&1 > O10.TXT'
  'x=$(cook pair.elf A.TXT B.TXT 2>&1 > O11.TXT) ; cook call.elf "$x" > X11.TXT'
)
FILES="E1 O2 E3 O4 E4 O5 E6 O6 E7 S7 W8 O9 O10 O11 X11"
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("cook pair.elf A.TXT B.TXT" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
( cd "$WORK/host" && export LC_ALL=C && for l in "${LINES[@]}"; do
    b=${l#cook }; b=${b//cook pair.elf/cmp}; b=${b//pair.elf/cmp}; b=${b//raise.elf/tr a-z A-Z}; b=${b//skim.elf/head}
    b=${b//; cook call.elf/; echo}; b=${b//call.elf/echo}
    bash -c "$b" > ../host.out 2>&1      # not /dev/null: GNU cmp goes silent when stdout is
  done )
bad=0
for f in $FILES; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    sed 's/^cmp:/pair:/; s/^CMP:/PAIR:/' "$WORK/host/$f.TXT" > "$WORK/want.$f"
    cmp -s "$WORK/got.$f" "$WORK/want.$f" || { echo "        $f.TXT differs: got '$(cat "$WORK/got.$f")', bash '$(cat "$WORK/want.$f")'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#LINES[@]} lines write what bash writes ($(echo $FILES | wc -w) files: 2>, 2>>, > f 2>&1, 2>&1 into a pipe, 2> on a pipe's first stage, an empty 2> file, the status, a2> as a word, 2>&1 > f in sh's order)" \
    || { echo "  FAIL  2> differs from bash"; fail=1; }
tr -d '\r' < "$WORK/serial.log" | tail -n 8 | grep -F "pair: EOF on 'B.TXT' after byte 2, line 1" >/dev/null \
    && echo "  ok    without 2> the complaint still goes to the console" \
    || { echo "  FAIL  without 2> the complaint did not reach the console"; fail=1; }
exit $fail

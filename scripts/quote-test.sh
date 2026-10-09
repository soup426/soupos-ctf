#!/usr/bin/env bash
# quote-test.sh - quotes in the shell, against sh (v0.57.2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-quote.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'a pot of soup\nno pot here\na  pot with two spaces\n' > "$WORK/G.TXT"
mcopy -i "$WORK/disk.img" "$WORK/G.TXT" ::/G.TXT
# soupOS line (output to /Qn.TXT) # sh line (output to Qn.TXT, files relative)
CASES=(
  "1#cook call.elf \"two  words\" here > /Q1.TXT#echo \"two  words\" here > Q1.TXT"
  "2#cook call.elf 'single  quoted' > /Q2.TXT#echo 'single  quoted' > Q2.TXT"
  "3#cook sift.elf \"a pot\" /G.TXT > /Q3.TXT#grep -F \"a pot\" G.TXT > Q3.TXT"
  "4#cook swap.elf ' ' _ < /G.TXT > /Q4.TXT#tr ' ' _ < G.TXT > Q4.TXT"
  "5#cook call.elf \"a|b\" 'c;d' \"e&&f\" > /Q5.TXT#echo \"a|b\" 'c;d' \"e&&f\" > Q5.TXT"
  "6#cook call.elf '\$?' \"\$?\" > /Q6.TXT#echo '\$?' \"\$?\" > Q6.TXT"
  "7#cook call.elf hi > \"/my file.txt\"#echo hi > \"my file.txt\""
  "8#cook spoon.elf \"/my file.txt\" > /Q8.TXT#cat \"my file.txt\" > Q8.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("$soup" "WAIT:2"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; cp "$WORK/G.TXT" "$WORK/host/"
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; ( cd "$WORK/host" && LC_ALL=C sh -c "$host" < /dev/null ); done
bad=0
for n in 1 2 3 4 5 6 8; do
    mcopy -n -i "$WORK/disk.img" "::/Q$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    cmp -s "$WORK/got$n" "$WORK/host/Q$n.TXT" || { echo "        differs: case $n  got [$(cat "$WORK/got$n")]  sh [$(cat "$WORK/host/Q$n.TXT")]"; bad=1; }
done
mcopy -n -i "$WORK/disk.img" "::/my file.txt" "$WORK/got7" 2>/dev/null || : > "$WORK/got7"
cmp -s "$WORK/got7" "$WORK/host/my file.txt" || { echo "        differs: case 7 (the file named with a space)"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    all 8 lines match sh: spaces kept inside quotes, grep and tr with quoted arguments, | ; && inside quotes, '\$?' kept, a file name with a space" \
    || { echo "  FAIL  quoting differs from sh"; fail=1; }
exit $fail

#!/usr/bin/env bash
# skimdregs-test.sh - skim -c, -n -N, -c -N and dregs -c, -n +N, -c +N
# against GNU head and tail, byte for byte (v0.60.128): text, 5000 random
# bytes, a last line with no newline, an empty file, counts of 0 and past
# the end, and stdin.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-skimdregs.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
seq -f 'line %g' 20 > "$H/t.txt"; head -c 5000 /dev/urandom > "$H/b.bin"; printf 'a\nb\nc' > "$H/n.txt"; : > "$H/e.txt"
for f in t.txt b.bin n.txt e.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
CASES=(
 "skim.elf -n 3 /t.txt" "skim.elf -n 0 /t.txt" "skim.elf -n -3 /t.txt" "skim.elf -n -50 /t.txt" "skim.elf -n -1 /n.txt"
 "skim.elf -c 5 /t.txt" "skim.elf -c 0 /t.txt" "skim.elf -c -5 /t.txt" "skim.elf -c 600 /b.bin" "skim.elf -c -4000 /b.bin"
 "skim.elf -c 3 /e.txt" "skim.elf -n -2 /e.txt"
 "dregs.elf -n 3 /t.txt" "dregs.elf -n +3 /t.txt" "dregs.elf -n +0 /t.txt" "dregs.elf -n +25 /t.txt" "dregs.elf -n +2 /n.txt"
 "dregs.elf -c 5 /t.txt" "dregs.elf -c +5 /t.txt" "dregs.elf -c 7000 /b.bin" "dregs.elf -c 600 /b.bin" "dregs.elf -c +4990 /b.bin"
 "dregs.elf -c 3 /e.txt" "dregs.elf -n +1 /e.txt"
)
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'cook %s > /o%d\n' "$c" $k >> "$H/r.sh"; done
printf 'cook spoon.elf /b.bin | cook dregs.elf -c 300 > /p1\ncook spoon.elf /t.txt | cook skim.elf -n -4 > /p2\ncook spoon.elf /t.txt | cook dregs.elf -n +18 > /p3\n' >> "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0; i=0
host() { local c=$1; c=${c/skim.elf/head}; c=${c/dregs.elf/tail}; c=${c// \// }; (cd "$H" && eval "$c"); }
for c in "${CASES[@]}"; do
  i=$((i+1))
  mcopy -n -i "$WORK/disk.img" "::/o$i" "$WORK/g$i" 2>/dev/null || { echo "        $c wrote nothing"; bad=1; continue; }
  host "$c" > "$WORK/w$i" 2>/dev/null
  cmp -s "$WORK/w$i" "$WORK/g$i" || { echo "        $c: $(wc -c < "$WORK/g$i") bytes, GNU's $(wc -c < "$WORK/w$i")"; bad=1; }
done
(cd "$H" && tail -c 300 b.bin) > "$WORK/wp1"; (cd "$H" && head -n -4 t.txt) > "$WORK/wp2"; (cd "$H" && tail -n +18 t.txt) > "$WORK/wp3"
for n in 1 2 3; do mcopy -n -i "$WORK/disk.img" "::/p$n" "$WORK/gp$n" 2>/dev/null; cmp -s "$WORK/wp$n" "$WORK/gp$n" || { echo "        stdin case p$n differs"; bad=1; }; done
[ "$bad" = 0 ] && echo "  ok    skim and dregs are GNU head and tail ($k cases and 3 from stdin, byte for byte: -c, -n -N, -c -N, -n +N, -c +N, 0, past the end, binary, empty)" \
    || { echo "  FAIL  skim or dregs differ"; fail=1; }
exit $fail

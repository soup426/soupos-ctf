#!/usr/bin/env bash
# peelstalk-test.sh - peel.elf and stalk.elf against the host's basename and
# dirname (v0.60.105), edge cases and all.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-peelstalk.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
NAMES=('/home/chef/soup.txt' 'soup.txt' '/' '//' 'a/' '/a' '/a/b/' 'a//b' '' '///x///' '.' '..' '/usr/lib/')
SUFFIXED=('soup.txt .txt' '.txt .txt' 'a.tar.gz .gz' 'dir/ .txt' '/x/y.c y.c')
LINES=(); n=0
for x in "${NAMES[@]}"; do n=$((n+1)); LINES+=("slurp \"B$n:[\$(cook peel.elf \"$x\")]\" \"D$n:[\$(cook stalk.elf \"$x\")]\""); done
for x in "${SUFFIXED[@]}"; do n=$((n+1)); set -- $x; LINES+=("slurp \"B$n:[\$(cook peel.elf \"$1\" \"$2\")]\""); done
LINES+=('slurp M:[$(cook stalk.elf /a/b c /d/)]')
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^(B[0-9]+|M):' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook peel.elf/basename}; b=${b//cook stalk.elf/dirname}; b=${b//slurp/echo}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^(B[0-9]+|M):' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    peel and stalk are the host's basename and dirname ($(wc -l < "$WORK/want") lines: 13 names, 5 with a suffix, several names at once)"
else echo "  FAIL  peel or stalk differ from the host's:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

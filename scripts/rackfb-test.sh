#!/usr/bin/env bash
# rackfb-test.sh - rack -f and -b (sort -f, sort -b) against GNU sort,
# LC_ALL=C (v0.60.133): alone, with -r -u -n, and in -k keys with and
# without -t.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-rackfb.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'banana\nApple\napple\n  cherry\nBanana\n\tdate\n Elder\nelder\n_under\n[bracket\nzeta\n  Apple\n' > "$H/w.txt"
printf 'x   b 3\ny a 10\nz  B 2\nw\tA 7\nv c 1\nu   a 5\n' > "$H/k.txt"
mcopy -i "$WORK/disk.img" "$H/w.txt" ::/w.txt; mcopy -i "$WORK/disk.img" "$H/k.txt" ::/k.txt
CASES=("-f /w.txt" "-b /w.txt" "-fb /w.txt" "-f -r /w.txt" "-fu /w.txt" "-bf -u /w.txt" "-b -k 2 /k.txt" "-k 2 /k.txt"
       "-f -k 2 /k.txt" "-fb -k 2,2 /k.txt" "-b -k 3 -n /k.txt" "-t ' ' -f -k 2 /k.txt")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook rack.elf %s\n' $k "$c" >> "$H/r.sh"; done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/^cook rack.elf/sort/; s| /\([wk]\).txt$| \1.txt|' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash) > "$WORK/want" 2>/dev/null
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    rack -f and -b are GNU sort's ($k cases, $(wc -l < "$WORK/want") lines: alone, with -r -u -n, in -k keys with and without -t)"
else echo "  FAIL  rack differs from sort:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

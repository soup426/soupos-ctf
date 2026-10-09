#!/usr/bin/env bash
# cullui-test.sh - cull -u and -i (uniq -u, uniq -i) against GNU uniq
# (v0.60.129), alone and with -c and -d: runs of different case, a last
# line with no newline, an empty file.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cullui.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'apple\napple\nBanana\nbanana\nBANANA\ncherry\nDate\ndate\nelder\nelder\nfig' > "$H/t.txt"; : > "$H/e.txt"
mcopy -i "$WORK/disk.img" "$H/t.txt" ::/t.txt; mcopy -i "$WORK/disk.img" "$H/e.txt" ::/e.txt
CASES=("-u /t.txt" "-i /t.txt" "-iu /t.txt" "-ic /t.txt" "-id /t.txt" "-du /t.txt" "-uc /t.txt" "-ci -u /t.txt" "-u /e.txt" "-i /e.txt" "/t.txt")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook cull.elf %s\n' $k "$c" >> "$H/r.sh"; done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/^cook cull.elf/uniq/; s| /\([te]\).txt$| \1.txt|' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash) > "$WORK/want" 2>/dev/null
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    cull -u and -i are GNU uniq's ($k cases, $(wc -l < "$WORK/want") lines: alone, with -c and -d, -d -u, no last newline, empty)"
else echo "  FAIL  cull differs from uniq:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

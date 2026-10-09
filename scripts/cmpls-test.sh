#!/usr/bin/env bash
# cmpls-test.sh - pair -l and -s against GNU cmp in the C locale (v0.60.139):
# every differing byte with the offset width from the smaller file, high
# bytes, an EOF after differences, -s silent with each status, -ls refused.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cmpls.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'abc\ndef\n' > "$H/c1"; printf 'abX\ndeY\nmore\n' > "$H/c2"; printf 'ab' > "$H/c3"
# a difference near the end too: GNU cmp 3.12 -l says 0 for a file over its
# buffer whose last piece matches (its bug; pair says 1 whenever one differs)
head -c 20000 /dev/zero > "$H/e1"; { head -c 9 "$H/e1"; printf '\377'; head -c 9990 "$H/e1"; printf 'x'; head -c 9989 "$H/e1"; printf 'y'; head -c 9 "$H/e1"; } > "$H/e2"
for f in c1 c2 c3 e1 e2; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
CASES=("-l c1 c2" "-l c2 c1" "-l e1 e2" "-l c1 c3" "-l c1 c1" "-s c1 c2" "-s c1 c1" "-s c1 c3" "-s c1 nope" "-ls c1 c2")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook pair.elf %s\nslurp "st $?"\n' $k "$c" >> "$H/r.sh"; done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'cd / ; follow /r.sh > /out.txt 2> /err.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/cook pair.elf/cmp/' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash 2>/dev/null) > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | head -20 | sed 's/^/        /'; bad=1; }
# the EOF after -l's differences, as GNU says it: no line part
grep -F "EOF on 'c3' after byte 2" "$WORK/serial.log" >/dev/null || tr -d '\r' < "$WORK/serial.log" | grep -F "EOF on 'c3' after byte 2" >/dev/null || { echo "        -l's EOF message was not seen"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    pair -l and -s are GNU cmp's ($k cases with statuses: offset width from the smaller file, high bytes, EOF after differences, -s silent, -ls refused)" \
    || { echo "  FAIL  pair differs from cmp"; fail=1; }
exit $fail

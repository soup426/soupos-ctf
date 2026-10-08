#!/usr/bin/env bash
# uniq-test.sh - cull.elf against the host's uniq (v0.56.5).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-uniq.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# Adjacent and non-adjacent repeats, repeated empty lines, and a last line
# with no newline equal to the one before it.
printf 'a\na\nb\na\n\n\nc\nc\nc\nb\nlast\nlast' > "$WORK/U1.TXT"
printf 'only one, no newline' > "$WORK/U2.TXT"
: > "$WORK/U3.TXT"
for f in U1 U2 U3; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
CASES=(
  "1#cull.elf /U1.TXT#uniq U1.TXT"
  "2#cull.elf -c /U1.TXT#uniq -c U1.TXT"
  "3#cull.elf -d /U1.TXT#uniq -d U1.TXT"
  "4#cull.elf -c -d /U1.TXT#uniq -c -d U1.TXT"
  "5#rack.elf /U1.TXT | cull.elf -c#sort U1.TXT | uniq -c"
  "6#cull.elf /U2.TXT#uniq U2.TXT"
  "7#cull.elf /U3.TXT#uniq U3.TXT"
  "8#spoon.elf /U1.TXT | cull.elf#uniq U1.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /Q$n.TXT" "WAIT:2"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/Q$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && LC_ALL=C bash -c "$host" < /dev/null ) > "$WORK/want$n"
    if cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "  ok    $soup matches $host"
    else echo "  FAIL  $soup differs from $host"; diff "$WORK/want$n" "$WORK/got$n" | head -6 | sed 's/^/        /'; fail=1; fi
done
exit $fail

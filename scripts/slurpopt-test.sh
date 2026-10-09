#!/usr/bin/env bash
# slurpopt-test.sh - slurp -n, -e and -E against bash's echo (v0.60.84),
# byte for byte through { ... } > F.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-slurpopt.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  '{ slurp -n a ; slurp b ; } > /e1.txt'
  '{ slurp -e "x\ty\\\\z" ; } > /e2.txt'
  '{ slurp -ne "p\nq" ; slurp -E "r\ns" ; } > /e3.txt'
  '{ slurp -e "stop\cnever" ; slurp after ; } > /e4.txt'
  '{ slurp -x hi ; slurp -- hi ; slurp - ; slurp -nx ok ; } > /e5.txt'
  '{ slurp "-n" quoted ; slurp ; } > /e6.txt'
  '{ slurp -e "\x41\0102\e." ; } > /e7.txt'
  '{ slurp -e "a\qb" ; slurp -e back\\\\slash ; slurp -e -n "\x4" ; } > /e8.txt'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/b"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//\/e/$WORK\/b\/e}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null
bad=0
for k in 1 2 3 4 5 6 7 8; do
    mcopy -n -i "$WORK/disk.img" "::/e$k.txt" "$WORK/got$k" 2>/dev/null || : > "$WORK/got$k"
    cmp -s "$WORK/got$k" "$WORK/b/e$k.txt" || { echo "        e$k: got $(od -c "$WORK/got$k" | head -2 | tr -s ' ' | tr '\n' ' ')"; echo "            bash $(od -c "$WORK/b/e$k.txt" | head -2 | tr -s ' ' | tr '\n' ' ')"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    slurp's -n -e -E are bash's echo's, byte for byte (8 files: -n, \\t \\\\, -ne and -E, \\c, words that are not options, a quoted -n, \\x \\0 \\e, unknown escapes)" \
    || { echo "  FAIL  slurp's options differ from bash's echo"; fail=1; }
exit $fail

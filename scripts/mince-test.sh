#!/usr/bin/env bash
# mince-test.sh - mince.elf against the host's hexdump -C, byte for byte (v0.60.14).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-mince.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
export LC_ALL=C
cp disk.img "$WORK/disk.img"
printf 'Hello\n' > "$WORK/F1.TXT"                                   # one short line
: > "$WORK/F2.TXT"                                                 # empty
head -c 70 /dev/zero > "$WORK/F3.TXT"                              # repeats, then a short tail
{ head -c 32 /dev/zero; printf abc; head -c 32 /dev/zero; } > "$WORK/F4.TXT"   # a star, then a break
head -c 4096 /dev/urandom > "$WORK/RAND.BIN"                       # every byte value, past a read
printf '0123456789abcdef' > "$WORK/F5.TXT"                         # exactly one line
for f in F1.TXT F2.TXT F3.TXT F4.TXT F5.TXT RAND.BIN; do mcopy -i "$WORK/disk.img" "$WORK/$f" "::/$f"; done
mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT "$WORK/HELLO.TXT"            # one already on the disk
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#mince.elf /F1.TXT#hexdump -C F1.TXT"
  "2#mince.elf /F2.TXT#hexdump -C F2.TXT"
  "3#mince.elf /F3.TXT#hexdump -C F3.TXT"
  "4#mince.elf /F4.TXT#hexdump -C F4.TXT"
  "5#mince.elf /F5.TXT#hexdump -C F5.TXT"
  "6#mince.elf /RAND.BIN#hexdump -C RAND.BIN"
  "7#spoon.elf /RAND.BIN | mince.elf#hexdump -C RAND.BIN"
  "8#mince.elf /HELLO.TXT#hexdump -C HELLO.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
keys+=("cook mince.elf /NOPE.TXT" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/H$n.TXT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && bash -c "$host" < /dev/null ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "        differs: $soup  (host: $host)"; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's hexdump -C byte for byte (empty, one line exactly, short tails, * for repeats, every byte value, a pipe)" \
    || { echo "  FAIL  mince differs from the host's hexdump -C"; fail=1; }
if grep -q 'mince: cannot open /NOPE.TXT' "$WORK/serial.log"; then echo "  ok    a missing file says so"
else echo "  FAIL  no 'mince: cannot open' for a missing file"; fail=1; fi
exit $fail

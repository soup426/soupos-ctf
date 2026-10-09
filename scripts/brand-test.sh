#!/usr/bin/env bash
# brand-test.sh - brand.elf (SHA-256) against the host's sha256sum
# (v0.60.107): empty, "abc", the 55/56/64-byte padding edges, 100 KB of
# random bytes, several files at once, stdin, and one not there (status 1).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-brand.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
: > "$H/e.txt"; printf abc > "$H/a.txt"
for n in 55 56 64; do head -c $n /dev/zero | tr '\0' 'x' > "$H/b$n"; done
head -c 102400 /dev/urandom > "$H/big.bin"
for f in e.txt a.txt b55 b56 b64 big.bin; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
keys=("headchef" "rosemary" "WAIT:1"
  'cook brand.elf /e.txt /a.txt /b55 /b56 /b64 /big.bin > /sums.txt' "UNTIL:@soupOS:"
  'cook spoon.elf /a.txt | cook brand.elf > /in.txt' "UNTIL:@soupOS:"
  'cook brand.elf /nope /a.txt > /m.txt ; slurp S:$?' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
want() { for f in "$@"; do printf '%s  /%s\n' "$(sha256sum "$H/$f" | cut -d' ' -f1)" "$f"; done; }
mcopy -n -i "$WORK/disk.img" ::/sums.txt "$WORK/g.sums" && want e.txt a.txt b55 b56 b64 big.bin | cmp -s - "$WORK/g.sums" \
    || { echo "        the six files' sums differ:"; want e.txt a.txt b55 b56 b64 big.bin | diff - "$WORK/g.sums" | sed 's/^/          /'; bad=1; }
mcopy -n -i "$WORK/disk.img" ::/in.txt "$WORK/g.in" && [ "$(cat "$WORK/g.in")" = "$(sha256sum < "$H/a.txt")" ] || { echo "        stdin's sum differs"; bad=1; }
mcopy -n -i "$WORK/disk.img" ::/m.txt "$WORK/g.m" && want a.txt | cmp -s - "$WORK/g.m" || { echo "        the file after a missing one differs"; bad=1; }
tr -d '\r' < "$WORK/serial.log" | grep -x 'S:1' >/dev/null || { echo "        a missing file was not status 1"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    brand is the host's sha256sum (empty, abc, 55/56/64 bytes, 100 KB random, six at once, stdin as -, a missing file skipped with status 1)" \
    || { echo "  FAIL  brand differs from sha256sum"; fail=1; }
exit $fail

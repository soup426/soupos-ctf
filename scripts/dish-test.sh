#!/usr/bin/env bash
# dish-test.sh - dish.elf against the host's printf (coreutils), byte for byte (v0.60.49).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-dish.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
# n # the arguments, typed the same in soupOS (after `cook dish.elf`) and given to env printf
CASES=(
  '1#"%s-%d\n" a 5'
  '2#"[%5s][%-5s]\n" ab cd'
  '3#"%05d|%-5d|%5d\n" 42 42 -42'
  '4#"%x %o %i\n" 255 8 77'
  '5#"%c%c\n" hello world'
  '6#"%.2s|%.5d|%8.3s|\n" abcdef 42 xyzzy'
  '7#"%s=%d\n" a 1 b 2 c 3'
  '8#"%s|%d|%s|\n" only'
  '9#"plain\n" extra args'
  "10#'a\\tb\\\\c\\n'"
  '15#"before\cafter\n"'
  '11#"100%%\n"'
  '12#"%06d|%-6d|\n" -42 -42'
  '13#"%3c|%-3c|\n" x y'
  '14#"%d%%\n" 7'
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do n=${c%%#*}; a=${c#*#}; keys+=("cook dish.elf $a > /D$n.TXT" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    n=${c%%#*}; a=${c#*#}
    ( cd "$WORK/host" && LC_ALL=C bash -c "env printf $a" > "D$n.TXT" 2>/dev/null )
    mcopy -n -i "$WORK/disk.img" "::/D$n.TXT" "$WORK/got$n" 2>/dev/null || { echo "        D$n.TXT not written"; bad=1; continue; }
    cmp -s "$WORK/got$n" "$WORK/host/D$n.TXT" || { echo "        $a: got '$(od -c "$WORK/got$n" | head -2 | tr -s ' ')', printf '$(od -c "$WORK/host/D$n.TXT" | head -2 | tr -s ' ')'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} formats match the host's printf byte for byte (%s %d %i %x %o %c %%, widths, - and 0, precision, reuse, missing and extra args, escapes)" \
    || { echo "  FAIL  dish.elf differs from printf"; fail=1; }
exit $fail

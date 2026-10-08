#!/usr/bin/env bash
# find-test.sh - forage.elf against the host's find, both sorted (v0.59.2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-find.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host/T/sub/deeper" "$WORK/host/T/empty"
for f in T/a.txt T/sub/b.txt T/sub/x.txt T/sub/deeper/c.dat T/sub/deeper/c.txt; do echo "$f" > "$WORK/host/$f"; done
mcopy -s -i "$WORK/disk.img" "$WORK/host/T" ::/
CASES=(
  "1#/T#T"
  "2#/T -name '*.txt'#T -name '*.txt'"
  "3#/T -type d#T -type d"
  "4#/T -type f#T -type f"
  "5#/T/sub -name 'c*'#T/sub -name 'c*'"
)
keys=("headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook forage.elf $soup > /F$n.OUT" "WAIT:2"); done
keys+=("clockout" "WAIT:1" "cook" "soup" "WAIT:2" "cook forage.elf /home > ~/FH.OUT ; cook call.elf \$? > ~/FHS.OUT" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    got=$(val "/F$n.OUT" | sed 's|^/||' | LC_ALL=C sort)
    want=$(cd "$WORK/host" && bash -c "find $host" | LC_ALL=C sort)
    [ "$got" = "$want" ] || { echo "        find $soup:"; diff <(echo "$want") <(echo "$got") | head -4 | sed 's/^/          /'; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms list what the host's find lists (sorted): the tree, -name, -type d, -type f, a sub-bowl" \
    || { echo "  FAIL  forage.elf differs from the host's find"; fail=1; }
FH=$(val /home/cook/FH.OUT)
echo "$FH" | grep -x "/home/saucier" >/dev/null && echo "$FH" | grep -x "/home/cook" >/dev/null && [ "$(val /home/cook/FHS.OUT | tr -d '\n')" = "1" ] \
    && tr -d '\r' < "$WORK/serial.log" | grep "forage: cannot list /home/saucier" >/dev/null \
    && echo "  ok    for the cook, a closed home is listed but not entered: reported, exit 1, the rest carries on" \
    || { echo "  FAIL  find /home as the cook: [$(echo "$FH" | tr '\n' ' ')] status $(val /home/cook/FHS.OUT)"; fail=1; }
exit $fail

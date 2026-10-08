#!/usr/bin/env bash
# control-test.sh - for loops and if blocks, against sh (v0.59.3).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-control.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host/G"; for f in a.txt b.txt c.dat; do echo "$f" > "$WORK/host/G/$f"; done
mcopy -s -i "$WORK/disk.img" "$WORK/host/G" ::/
LINES=(
  "for f in a b c ; do cook echo.elf \$f >> /L1.TXT ; done"
  "for f in /G/*.txt ; do cook echo.elf item \$f >> /L2.TXT ; done"
  "if cook test.elf -f /G/a.txt ; then cook echo.elf yes > /L3.TXT ; else cook echo.elf no > /L3.TXT ; fi"
  "if cook test.elf -f /NOPE ; then cook echo.elf yes > /L4.TXT ; else cook echo.elf no > /L4.TXT ; fi"
  "for n in 1 2 3 4 ; do if cook test.elf \$n -gt 2 ; then cook echo.elf big \$n >> /L5.TXT ; else cook echo.elf small \$n >> /L5.TXT ; fi ; done"
  "if cook test.elf -d /G ; then for f in x \"y  z\" ; do cook echo.elf v=\$f >> /L6.TXT ; done ; fi"
  "for f in a ; do cook hello.elf ; done ; cook echo.elf \$? > /L7.TXT"
  "if cook hello.elf ; then cook echo.elf t > /L8X.TXT ; fi ; cook echo.elf \$? > /L8.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "WAIT:3"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
for l in "${LINES[@]}"; do
    h=$(echo "$l" | sed -e 's|cook test.elf|test|g' -e 's|cook echo.elf|echo|g' -e 's|cook hello.elf|(exit 42)|g' -e 's| /G| G|g' -e 's| /NOPE| NOPE|g' -e 's| /L| L|g')
    ( cd "$WORK/host" && LC_ALL=C sh -c "$h" ) >/dev/null 2>&1
done
got=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null | grep -oE "L[0-9]+X?\.TXT" | sort | tr '\n' ' ')
want=$(cd "$WORK/host" && ls L*.TXT | sort | tr '\n' ' ')
bad=0; [ "$got" = "$want" ] || { echo "        files: soupOS [$got]  sh [$want]"; bad=1; }
for f in $want; do
    # soupOS's paths are absolute (/G/a.txt), sh's here relative (G/a.txt).
    g=$(mcopy -n -i "$WORK/disk.img" "::/$f" - 2>/dev/null | sed 's| /G/| G/|g'); w=$(cat "$WORK/host/$f")
    [ "$g" = "$w" ] || { echo "        $f: [$(echo "$g" | tr '\n' '|')]  sh [$(echo "$w" | tr '\n' '|')]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    eight lines leave what sh leaves: for, for over a glob, if/else both ways, if in for, for in if (a quoted word), the status after each" \
    || { echo "  FAIL  control flow differs from sh"; fail=1; }
exit $fail

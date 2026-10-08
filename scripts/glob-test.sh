#!/usr/bin/env bash
# glob-test.sh - * and ? in program arguments, against sh (v0.58.2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-glob.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host/G" "$WORK/host/MANY"
# Q.TXT, not B.TXT: FAT is case-insensitive, so b.txt and B.TXT are one name.
for f in a.txt ab.txt b.txt c.dat Q.TXT; do echo "$f" > "$WORK/host/G/$f"; done
for i in $(seq 1 30); do echo x > "$WORK/host/MANY/longish-name-$i.txt"; done
mcopy -s -i "$WORK/disk.img" "$WORK/host/G" "$WORK/host/MANY" ::/
# soupOS words (after cd /G) # the same words for the host's sh, in G
CASES=(
  "1#*.txt" "2#?.txt" "3#a*" "4#*" "5#*.none" "6#\"*.txt\"" "7#*.TXT"
)
keys=("headchef" "rosemary" "WAIT:1" "cd /G" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n words <<< "$c"; keys+=("cook call.elf $words > /O$n.OUT" "WAIT:2"); done
keys+=("cook call.elf /G/*.dat > /O8.OUT" "WAIT:2" "cook call.elf /MANY/* > /O9.OUT ; cook call.elf \$? > /O9S.OUT" "WAIT:3")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n words <<< "$c"
    got=$(val "/O$n.OUT"); want=$(cd "$WORK/host/G" && LC_ALL=C sh -c "echo $words")
    [ "$got" = "$want" ] || { echo "        echo $words: [$got]  sh [$want]"; bad=1; }
done
[ "$(val /O8.OUT)" = "/G/c.dat" ] || { echo "        echo /G/*.dat: [$(val /O8.OUT)]"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    globs expand as sh does: *.txt ?.txt a* * (byte order), no match kept, quoted kept, case kept, a bowl in front" \
    || { echo "  FAIL  globs differ from sh"; fail=1; }
tr -d '\r' < "$WORK/serial.log" | grep "The arguments are too long" >/dev/null && [ "$(val /O9S.OUT)" = "1" ] && [ -z "$(val /O9.OUT)" ] \
    && echo "  ok    an expansion past 128 bytes is refused with status 1, not cut short" || { echo "  FAIL  the overlong expansion"; fail=1; }
exit $fail

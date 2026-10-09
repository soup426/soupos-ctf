#!/usr/bin/env bash
# headtail-test.sh - skim.elf and dregs.elf against the host's (v0.56.2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-headtail.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
seq 1 25 | sed 's/^/line /' > "$WORK/F1.TXT"                      # 25 lines
printf 'one\ntwo\nthree, no newline' > "$WORK/F2.TXT"              # 3 lines, no final newline
: > "$WORK/F3.TXT"                                                 # empty
seq 1 2000 | sed 's/$/ of the big one/' > "$WORK/BIG.TXT"         # past tail's first 4 KB
{ echo first; head -c 300000 /dev/zero | tr '\0' w; echo; echo last; } > "$WORK/WIDE.TXT"   # one 300 KB line
for f in F1 F2 F3 BIG WIDE; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
# name # soupOS command # host command  (# because the commands contain |)
CASES=(
  "1#skim.elf /F1.TXT#head F1.TXT"
  "2#skim.elf -n 3 /F1.TXT#head -n 3 F1.TXT"
  "3#skim.elf -n 5 /F2.TXT#head -n 5 F2.TXT"
  "4#skim.elf /F3.TXT#head F3.TXT"
  "5#dregs.elf /F1.TXT#tail F1.TXT"
  "6#dregs.elf -n 3 /F1.TXT#tail -n 3 F1.TXT"
  "7#dregs.elf -n 2 /F2.TXT#tail -n 2 F2.TXT"
  "8#dregs.elf -n 5 /F2.TXT#tail -n 5 F2.TXT"
  "9#dregs.elf -n 0 /F1.TXT#tail -n 0 F1.TXT"
  "10#dregs.elf -n 7 /BIG.TXT#tail -n 7 BIG.TXT"
  "11#spoon.elf /F1.TXT | skim.elf -n 4#head -n 4 F1.TXT"
  "12#spoon.elf /BIG.TXT | dregs.elf -n 3#tail -n 3 BIG.TXT"
  "13#skim.elf -n 1500 /BIG.TXT | dregs.elf -n 2#head -n 1500 BIG.TXT | tail -n 2"
  "14#tally.elf 1000000 | label.elf | dregs.elf -n 3#seq 1000000 | nl -ba | tail -n 3"
  "15#dregs.elf -n 2 /WIDE.TXT#tail -n 2 WIDE.TXT"
  "16#spoon.elf /WIDE.TXT | dregs.elf -n 1#tail -n 1 WIDE.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /H$n.TXT" "UNTIL:headchef@soupOS:/> "); done
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
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's head and tail byte for byte (short files, no final newline, empty, past 4 KB, -n 0, pipes, a million lines, a 300 KB line)" \
    || { echo "  FAIL  head/tail differ from the host"; fail=1; }
exit $fail

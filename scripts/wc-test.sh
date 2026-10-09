#!/usr/bin/env bash
# wc-test.sh - weigh.elf against the host's wc, flag by flag (v0.56.6).
#
# Files are named relative to the cwd (/), so soupOS and the host print the
# same name. Pipes are not compared: GNU wc pads to 7 for a pipe and weigh.elf
# cannot tell a pipe from a file, so it always uses the file rule.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-wc.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
seq 1 25 | sed 's/^/line /' > "$WORK/F1.TXT"
printf 'one two\nthree' > "$WORK/F2.TXT"
: > "$WORK/F3.TXT"
seq 1 20000 > "$WORK/BIG.TXT"
printf 'tabs\there\r\nand\fform feeds\vtoo\n' > "$WORK/WS.TXT"
for f in F1 F2 F3 BIG WS; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
CASES=(
  "1#weigh.elf F1.TXT#wc F1.TXT"
  "2#weigh.elf -l F1.TXT#wc -l F1.TXT"
  "3#weigh.elf -w F1.TXT#wc -w F1.TXT"
  "4#weigh.elf -c F1.TXT#wc -c F1.TXT"
  "5#weigh.elf -l -w F1.TXT#wc -l -w F1.TXT"
  "6#weigh.elf -l -c F1.TXT#wc -l -c F1.TXT"
  "7#weigh.elf -w -c F1.TXT#wc -w -c F1.TXT"
  "8#weigh.elf F2.TXT#wc F2.TXT"
  "9#weigh.elf F3.TXT#wc F3.TXT"
  "10#weigh.elf BIG.TXT#wc BIG.TXT"
  "11#weigh.elf < F1.TXT#wc < F1.TXT"
  "12#weigh.elf -l < BIG.TXT#wc -l < BIG.TXT"
  "13#weigh.elf WS.TXT#wc WS.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do IFS='#' read -r n soup host <<< "$c"; keys+=("cook $soup > /W$n.OUT" "WAIT:2"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
for c in "${CASES[@]}"; do
    IFS='#' read -r n soup host <<< "$c"
    mcopy -n -i "$WORK/disk.img" "::/W$n.OUT" "$WORK/got$n" 2>/dev/null || : > "$WORK/got$n"
    ( cd "$WORK" && LC_ALL=C bash -c "$host" ) > "$WORK/want$n"
    if ! cmp -s "$WORK/got$n" "$WORK/want$n"; then
        echo "        differs: $soup  got [$(cat "$WORK/got$n")]  host [$(cat "$WORK/want$n")]"; bad=1; fi
done
[ "$bad" = 0 ] && echo "  ok    all ${#CASES[@]} forms match the host's wc (each flag and pair, no final newline, empty, 20000 lines, stdin, odd whitespace)" \
    || { echo "  FAIL  weigh.elf differs from the host's wc"; fail=1; }
exit $fail

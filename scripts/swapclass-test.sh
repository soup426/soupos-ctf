#!/usr/bin/env bash
# swapclass-test.sh - swap's [:class:] sets, octal escapes and -c against
# GNU tr in the C locale, byte for byte (v0.60.132).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-swapclass.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'Hello, World! 123\tTabs\there.\nSOUP of the day: 42 (cents)\n  spaced   out  \\back\\slash\n\001ctl\177 high\300\377 end\nhex DEADbeef 0x1F\n' > "$H/t.txt"
mcopy -i "$WORK/disk.img" "$H/t.txt" ::/t.txt
CASES=("'[:lower:]' '[:upper:]'" "'[:upper:]' '[:lower:]'" "-d '[:digit:]'" "-d '[:punct:]'" "-s '[:space:]'"
       "-cd '[:alnum:]\\n'" "-c '[:alpha:]' '_'" "-cs '[:alpha:]' '\\n'" "'[:space:]' '.'" "-d '[:cntrl:]'"
       "'\\101-\\132' 'a-z'" "-d '\\\\'" "'[:xdigit:]' 'x'" "-d '[:blank:]'" "-cd '[:print:]\\n'" "-s '[:alpha:]'")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'cook spoon.elf /t.txt | cook swap.elf %s > /o%d\n' "$c" $k >> "$H/r.sh"; done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0; i=0
for c in "${CASES[@]}"; do
  i=$((i+1))
  mcopy -n -i "$WORK/disk.img" "::/o$i" "$WORK/g$i" 2>/dev/null || { echo "        tr $c wrote nothing"; bad=1; continue; }
  (cd "$H" && eval "LC_ALL=C tr $c < t.txt") > "$WORK/w$i" 2>/dev/null
  cmp -s "$WORK/w$i" "$WORK/g$i" || { echo "        tr $c differs:"; diff <(od -c "$WORK/w$i") <(od -c "$WORK/g$i") | head -6 | sed 's/^/          /'; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    swap's classes, octal and -c are GNU tr's ($k cases byte for byte: case both ways, -d and -s by class, -c with -d -s and translating, an octal range)" \
    || { echo "  FAIL  swap differs from tr"; fail=1; }
exit $fail

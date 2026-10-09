#!/usr/bin/env bash
# herestr-test.sh - cook prog <<< WORD against bash (v0.60.45).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-herestr.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
mcopy -n -i "$WORK/disk.img" ::/HELLO.TXT "$WORK/host/HELLO.TXT"
LINES=(
  'cook swap.elf " " "\n" <<< "c b a" | rack.elf > /H1.TXT'
  'cook raise.elf <<< "loud words" > /H2.TXT'
  'v="x  y" ; cook raise.elf <<< "$v" > /H3.TXT'
  'v=word ; cook raise.elf <<< $v > /H4.TXT'
  'cook raise.elf < /HELLO.TXT <<< "here wins" > /H5.TXT'
  'cook raise.elf <<< "lost" < /HELLO.TXT > /H6.TXT'
  'cook weigh.elf -c <<< "" > /H7.TXT'
  'cook stack.elf <<< "solo" | raise.elf > /H8.TXT'
)
FILES="H1 H2 H3 H4 H5 H6 H7 H8"
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
script=""
for l in "${LINES[@]}"; do
    b=${l//cook /}; b=${b//swap.elf/tr}; b=${b//rack.elf/sort}; b=${b//raise.elf/tr a-z A-Z}
    b=${b//weigh.elf -c/wc -c}; b=${b//stack.elf/tac}; b=${b//\//}
    script+="$b"$'\n'
done
( cd "$WORK/host" && LC_ALL=C bash -c "$script" ) >/dev/null 2>&1
bad=0
for f in $FILES; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" || { echo "        $f.TXT: got '$(tr '\n' '|' < "$WORK/got.$f")', bash '$(tr '\n' '|' < "$WORK/host/$f.TXT")'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#LINES[@]} lines write what bash writes ($(echo $FILES | wc -w) files: a pipeline, quoted, \"\$v\", \$v, the later of < and <<< wins both ways, empty, into a pipe)" \
    || { echo "  FAIL  <<< differs from bash"; fail=1; }
exit $fail

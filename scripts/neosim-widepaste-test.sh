#!/usr/bin/env bash
# neosim-widepaste-test.sh - a charwise paste that would make a line past 255
# columns is refused whole, not cut (v0.60.305, review R-N54). insert_text
# skipped every character text_insert_char refused, so on a line of 200 a,
# y$$p wrote 255 and said nothing; a register of several lines did the same
# where its last line met the rest of the line it went into. Here W1.TXT
# (200 a) gets 0y$$p and :w, and W2.TXT (200 x, short, 100 y) gets jvj60ly
# (short and 61 y) then gg0p, whose last line would be 61 + 199: both are
# refused with E1240, logged, and the buffers written after are the files.
# And W3.TXT, a paste that fits once split, is whole (v0.60.311).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neowidepaste.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
python3 -c "
import sys
open(sys.argv[1], 'w').write('a' * 200 + '\n')
open(sys.argv[2], 'w').write('x' * 200 + '\nshort\n' + 'y' * 100 + '\n')
open(sys.argv[3], 'w').write('abc\ndef\n' + 'z' * 255 + '\n')
open(sys.argv[4], 'w').write('abc\ndef\n' + 'z' * 99 + 'bc\nde' + 'z' * 156 + '\n')" "$WORK/w1" "$WORK/w2" "$WORK/w3" "$WORK/w3want"
mcopy -o -i "$WORK/disk.img" "$WORK/w1" ::W1.TXT; mcopy -o -i "$WORK/disk.img" "$WORK/w2" ::W2.TXT; mcopy -o -i "$WORK/disk.img" "$WORK/w3" ::W3.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /W1.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" 'TYPE:0y$$p' "WAIT:1" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /W1.TXT" "WAIT:1" \
    "TYPE::e /W2.TXT" "KEY:ret" "UNTIL:[neosim] open /W2.TXT" "WAIT:1" "TYPE:jvj60ly" "WAIT:1" "TYPE:gg0p" "WAIT:1" \
    "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /W2.TXT" "WAIT:1" \
    "TYPE::e /W3.TXT" "KEY:ret" "UNTIL:[neosim] open /W3.TXT" "WAIT:1" "TYPE:0lvjy" "WAIT:1" "TYPE:G099lP" "WAIT:1" \
    "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /W3.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | grep '\[neosim\]' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::W1.TXT "$WORK/a1" 2>/dev/null; mcopy -n -i "$WORK/disk.img" ::W2.TXT "$WORK/a2" 2>/dev/null
cmp -s "$WORK/w1" "$WORK/a1" && echo "  ok    y\$\$p on a line of 200 was refused: the buffer written after it is the file" \
    || { echo "  FAIL  W1.TXT: its line is $(head -1 "$WORK/a1" | tr -d '\n' | wc -c) columns (was 200)"; fail=1; }
cmp -s "$WORK/w2" "$WORK/a2" && echo "  ok    a two-line paste whose last line would pass 255 was refused: the buffer written after it is the file" \
    || { echo "  FAIL  W2.TXT changed: $(awk '{ printf "%d ", length($0) }' "$WORK/a2")columns (was 200 5 100)"; fail=1; }
# (v0.60.311, review R-N57) A paste across lines into a long line that fits
# once split: its first part went in before the split, past 255, and was
# dropped. As nvim: abc/def/255 z, 0lvjy G099lP gives 99 z + bc, de + 156 z.
mcopy -n -i "$WORK/disk.img" ::W3.TXT "$WORK/a3" 2>/dev/null
cmp -s "$WORK/w3want" "$WORK/a3" && echo "  ok    a two-line paste into a 255-column line that fits once split is whole, as nvim" \
    || { echo "  FAIL  W3.TXT: $(awk '{ printf "%d ", length($0) }' "$WORK/a3")columns (nvim: 3 3 101 158)"; fail=1; }
n=$(tr -d '\r' < "$WORK/log" | grep -c '\[neosim\] refused a paste past 255')
[ "$n" -ge 2 ] && echo "  ok    both refusals logged" || { echo "  FAIL  refusals logged: $n of 2"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-widepaste-test: PASS" || echo "neosim-widepaste-test: FAIL"
exit "$fail"

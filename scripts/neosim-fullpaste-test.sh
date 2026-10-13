#!/usr/bin/env bash
# neosim-fullpaste-test.sh - a linewise paste into a full buffer is refused,
# not a crash (v0.60.288, review R-N2). With 2048 lines (neosim's most) a p
# of a line could insert nothing, but the cursor still went to the line it
# would have made, one past the last, and the next read of it took neosim
# down with its unsaved work. Here a 2048-line file, yyGp, o and O (all
# refused), then :q, which quits: nothing marked changed (v0.60.296). And a
# 2047-line file with ggy2jGp: three lines where one fits, refused whole; it
# joined the register's lines into the last one, cut at 255 (v0.60.298, R-N50).
# And a 2046-line file with G3ofoo<Esc>: three lines where two fit, refused
# whole; it gave 000 and 000000 (v0.60.304, R-N53). And on 2045 lines,
# G3oa<CR>b<Esc>: the repeats' Enters need room too (v0.60.313, R-N61).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neofullpaste.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
seq 1001 3048 > "$WORK/full"; mcopy -o -i "$WORK/disk.img" "$WORK/full" ::FULL.TXT
seq 1001 3047 > "$WORK/full2"; mcopy -o -i "$WORK/disk.img" "$WORK/full2" ::FULL2.TXT
seq 1001 3046 > "$WORK/full3"; mcopy -o -i "$WORK/disk.img" "$WORK/full3" ::FULL3.TXT
seq 1001 3045 > "$WORK/full4"; mcopy -o -i "$WORK/disk.img" "$WORK/full4" ::FULL4.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
# (v0.60.301, review R-T39) Twice: the refusals then :w, so what the buffer
# holds reaches the disk for the cmp (after a :q nothing is written and the
# cmp compared the file with itself); then each file again, the refusals,
# and :e and :q, which need nothing marked changed.
full=("TYPE:yyGp" "WAIT:1" "TYPE:o" "KEY:esc" "TYPE:O" "KEY:esc" "WAIT:1")
full2=("TYPE:ggy2jGp" "WAIT:1")
full3=("TYPE:G3o000" "KEY:esc" "WAIT:1")    # (v0.60.304, review R-N53) three new lines where two fit; refused, 000 is three moves to the line start
timeout 200 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /FULL.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "${full[@]}" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /FULL.TXT" "WAIT:1" \
    "TYPE::e /FULL2.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL2.TXT" "${full2[@]}" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /FULL2.TXT" "WAIT:1" \
    "TYPE::e /FULL3.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL3.TXT" "${full3[@]}" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /FULL3.TXT" "WAIT:1" \
    "TYPE::e /FULL4.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL4.TXT" "TYPE:G3oa" "KEY:ret" "TYPE:b" "KEY:esc" "WAIT:1" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /FULL4.TXT" "WAIT:1" \
    "TYPE::e /FULL.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL.TXT" "WAIT:1" "${full[@]}" \
    "TYPE::e /FULL2.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL2.TXT" "${full2[@]}" \
    "TYPE::e /FULL3.TXT" "KEY:ret" "UNTIL:[neosim] open /FULL3.TXT" "${full3[@]}" \
    "TYPE::q" "KEY:ret" "UNTIL:[neosim] bye" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  neosim did not live to write: $(tr -d '\r' < "$WORK/log" | grep -E '\[neosim\]|fault|killed' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::FULL.TXT "$WORK/after" 2>/dev/null
mcopy -n -i "$WORK/disk.img" ::FULL2.TXT "$WORK/after2" 2>/dev/null
mcopy -n -i "$WORK/disk.img" ::FULL3.TXT "$WORK/after3" 2>/dev/null
# (v0.60.313, review R-N61) G3oa<CR>b<Esc> on 2045 lines: the first a and b
# fit, the two repeats (four lines) do not and are refused whole; they ran
# a, b, ab, the Enter replayed with no room.
mcopy -n -i "$WORK/disk.img" ::FULL4.TXT "$WORK/after4" 2>/dev/null
{ cat "$WORK/full4"; printf 'a\nb\n'; } > "$WORK/want4"
cmp -s "$WORK/want4" "$WORK/after4" && echo "  ok    3oa<CR>b on 2045 lines: the first a and b, the repeats refused whole (v0.60.313)" \
    || { echo "  FAIL  FULL4.TXT: $(wc -l < "$WORK/after4") lines, the last three '$(tail -3 "$WORK/after4" | tr '\n' ' ')' (want 2047, ending a b)"; fail=1; }
cmp -s "$WORK/full3" "$WORK/after3" && echo "  ok    3o into a 2046-line buffer was refused whole: the buffer written after it is as the file was (v0.60.304)" \
    || { echo "  FAIL  FULL3.TXT changed: $(wc -l < "$WORK/after3") lines, the last two '$(tail -2 "$WORK/after3" | tr '\n' ' ')'"; fail=1; }
cmp -s "$WORK/full2" "$WORK/after2" && echo "  ok    a 3-line paste into a 2047-line buffer was refused whole: the buffer written after it is as the file was (v0.60.298)" \
    || { echo "  FAIL  FULL2.TXT changed: $(tail -1 "$WORK/after2")"; fail=1; }
cmp -s "$WORK/full" "$WORK/after" && echo "  ok    p, o and O into a full buffer were refused: the buffer written after them is as the file was" \
    || { echo "  FAIL  the file changed: $(wc -l < "$WORK/after") lines"; fail=1; }
# (v0.60.296, review R-N49) a refusal changes nothing: no undo step, no [+]: :q quits.
tr -d '\r' < "$WORK/log" | grep -F '[neosim] refused :q' >/dev/null && { echo "  FAIL  :q was refused: a refused p, o or O marked the buffer changed"; fail=1; }
tr -d '\r' < "$WORK/log" | grep -F '[neosim] bye' >/dev/null && echo "  ok    :q quit after them: nothing was marked changed" || { echo "  FAIL  neosim did not quit"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-fullpaste-test: PASS" || echo "neosim-fullpaste-test: FAIL"
exit "$fail"

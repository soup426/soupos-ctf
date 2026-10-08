#!/usr/bin/env bash
# append-test.sh - >> appends, and redirection asks the bowl (v0.56.0).
#
# `>>` was not a token: the tokenizer split it into two `>`, so the second
# became the output file's name. And `>` to a NEW file never checked the
# bowl: proven on v0.55.6, the ordinary cook's `cook call.elf evil >
# /etc/EVIL.TXT` made the file in a bowl closed to cooks.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-append.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook call.elf one > /A.TXT" "WAIT:1" "cook call.elf two >> /A.TXT" "WAIT:1" "cook call.elf three >> /A.TXT" "WAIT:1" \
    "cook call.elf fresh >> /B.TXT" "WAIT:1" "cook call.elf one two three | weigh.elf >> /A.TXT" "WAIT:2" \
    "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "cook call.elf evil > /etc/EVIL.TXT" "WAIT:1" "cook call.elf evil >> /etc/EVIL2.TXT" "WAIT:1" \
    "cook call.elf mine >> /home/cook/M.TXT" "WAIT:1" "cook call.elf again >> /home/cook/M.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
# The host shell, the same commands.
( cd "$WORK" && echo one > want.txt && echo two >> want.txt && echo three >> want.txt )
mtype -i "$WORK/disk.img" ::/A.TXT > "$WORK/got.txt" 2>/dev/null
head -3 "$WORK/got.txt" | cmp -s - "$WORK/want.txt" \
    && echo "  ok    > then two >> give the same bytes as the host shell" || { echo "  FAIL  /A.TXT:"; cat -A "$WORK/got.txt" | sed 's/^/        /'; fail=1; }
# weigh.elf prints the host's format since v0.56.6.
[ "$(sed -n 4p "$WORK/got.txt")" = " 1  3 14" ] \
    && echo "  ok    a pipeline's output appends too" || { echo "  FAIL  pipeline >>"; fail=1; }
[ "$(mtype -i "$WORK/disk.img" ::/B.TXT 2>/dev/null)" = "fresh" ] \
    && echo "  ok    >> makes a file that is not there" || { echo "  FAIL  >> to a new file"; fail=1; }
[ "$(mtype -i "$WORK/disk.img" ::/home/cook/M.TXT 2>/dev/null | tr '\n' ' ')" = "mine again " ] \
    && echo "  ok    a cook appends in their own home" || { echo "  FAIL  the cook's own >>"; fail=1; }
! mdir -b -i "$WORK/disk.img" ::/etc 2>/dev/null | grep -i evil >/dev/null && [ "$(tr -d '\r' < "$WORK/serial.log" | grep -c "Permission denied: /etc$")" = 2 ] \
    && echo "  ok    neither > nor >> makes a file in a bowl the cook may not write" || { echo "  FAIL  a cook made a file in /etc"; fail=1; }
[ "$(fsck.fat -n "$WORK/disk.img" 2>&1 | wc -l)" -le 2 ] && echo "  ok    fsck is clean" || { echo "  FAIL  fsck"; fail=1; }
exit $fail

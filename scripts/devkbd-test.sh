#!/usr/bin/env bash
# devkbd-test.sh - a program reading /dev/kbd (v0.60.257, review R-K1). It
# used to read the keyboard with interrupts off inside the syscall, and the
# hlt waiting for a key never woke: the whole machine froze. It had no
# foreground check either, so a background program would have read what
# the console typed. Foreground: cook spoon.elf /dev/kbd gets a typed line,
# Ctrl-C ends it, and the shell answers after. Background: spoon reading
# /dev/kbd into /KEYS.TXT gets nothing, and a word typed at the shell after
# it is not in that file: the background spoon ends at once, the file is
# there and empty, the shell has the whole word (v0.60.261).
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-devkbd.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook spoon.elf /dev/kbd" "WAIT:2" "TYPE:kbdline" "KEY:ret" "WAIT:1" "KEY:ctrl-c" "WAIT:2" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" \
    "cook spoon.elf /dev/kbd > /KEYS.TXT &" "WAIT:3" "parsnipword" "WAIT:1" \
    "pour HELLO.TXT" "UNTIL:A warm bowl" "WAIT:1" >/dev/null 2>&1
# A failed drive is read below, not here: stolen keys also end it (a lost p
# broke the second pour), and the checks say which it was.
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
if grep -q 'cooking /spoon.elf' "$WORK/l"; then
    [ "$(grep -c 'A warm bowl' "$WORK/l")" -ge 2 ] && echo "  ok    the shell answers after both" || { echo "  FAIL  the shell did not answer twice: $(tail -2 "$WORK/l" | tr '\n' ' ')"; fail=1; }
    grep -q '^kbdline' "$WORK/l" && echo "  ok    the foreground spoon read the typed line" || { echo "  FAIL  the foreground spoon never printed kbdline"; fail=1; }
    # (v0.60.261, review R-T20) A spoon that steals keys never ends, so its
    # /KEYS.TXT is never written: a missing file proved nothing. The
    # background spoon must have ended (EOF, not the foreground) before the
    # word was typed, left /KEYS.TXT there and empty, and the shell must
    # have had the whole word.
    bpid=$(grep -oE '^\[proc [0-9]+\] spawned /spoon\.elf' "$WORK/l" | sed -n 2p | grep -oE '[0-9]+')
    ended=$(grep -n "^\[proc $bpid\] /spoon\.elf exited with code" "$WORK/l" | head -1 | cut -d: -f1)
    typed=$(grep -n 'parsnipword' "$WORK/l" | head -1 | cut -d: -f1)
    [ -n "$bpid" ] && [ -n "$ended" ] && [ -n "$typed" ] && [ "$ended" -lt "$typed" ] \
        && echo "  ok    the background spoon on /dev/kbd ended at once, before anything was typed" \
        || { echo "  FAIL  the background spoon (pid ${bpid:-?}) had not ended when parsnipword was typed (ended at line ${ended:-never}, typed at ${typed:-?})"; fail=1; }
    if mdir -i "$WORK/disk.img" ::/KEYS.TXT >/dev/null 2>&1; then
        keys=$(mcopy -n -i "$WORK/disk.img" ::/KEYS.TXT - 2>/dev/null)
        [ -z "$keys" ] && echo "  ok    its /KEYS.TXT is there and empty" || { echo "  FAIL  the background spoon read the console's keys: $keys"; fail=1; }
    else
        echo "  FAIL  no /KEYS.TXT: the background spoon never closed it"; fail=1
    fi
    grep -q 'parsnipword' "$WORK/l" && echo "  ok    the shell had the whole word" || { echo "  FAIL  the shell's echo lost part of the word (stolen keys): $(grep -o '[a-z]*nipword' "$WORK/l" | head -1)"; fail=1; }
else
    echo "  FAIL  spoon never started: $(tail -2 "$WORK/l" | tr '\n' ' ')"; fail=1
fi
[ "$fail" = 0 ] && echo "devkbd-test: PASS" || echo "devkbd-test: FAIL"
exit "$fail"

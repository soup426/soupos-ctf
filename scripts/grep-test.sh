#!/usr/bin/env bash
# grep-test.sh - sift.elf against the host's grep -F (v0.56.1; -n and -o v0.60.35).
#
# One input with the awkward cases: matches in different cases, an empty
# line, a 3000-byte line with the word in the middle, and a last line with
# no newline. Seven forms run inside soupOS, each into a file; each file is
# compared byte for byte with LC_ALL=C grep -F on the same input.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-grep.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
{ printf 'a pot of soup\n'; printf 'nothing here\n'; printf '\n'; printf 'Soup in capitals\n'
  printf 'x%.0s' $(seq 1 1500); printf 'soup'; printf 'y%.0s' $(seq 1 1500); printf '\n'
  printf 'SOUPSOUP\n'; printf 'the last line, soup, has no newline'; } > "$WORK/G.TXT"
mcopy -i "$WORK/disk.img" "$WORK/G.TXT" ::/G.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "cook sift.elf soup /G.TXT > /O1.TXT" "WAIT:2" \
    "cook sift.elf -v soup /G.TXT > /O2.TXT" "WAIT:2" \
    "cook sift.elf -c soup /G.TXT > /O3.TXT" "WAIT:2" \
    "cook sift.elf -i soup /G.TXT > /O4.TXT" "WAIT:2" \
    "cook sift.elf -c -i soup /G.TXT > /O5.TXT" "WAIT:2" \
    "cook sift.elf -v -i soup /G.TXT > /O6.TXT" "WAIT:2" \
    "cook spoon.elf /G.TXT | sift.elf soup > /O7.TXT" "WAIT:2" \
    "cook sift.elf -n soup /G.TXT > /O8.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -o soup /G.TXT > /O9.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -o -i soup /G.TXT > /O10.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -noi soup /G.TXT > /O11.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -o -v soup /G.TXT > /O12.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -o -c soup /G.TXT > /O13.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf -n -c -i soup /G.TXT > /O14.TXT" "UNTIL:@soupOS:" \
    "cook sift.elf nowhere /G.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
G="$WORK/G.TXT"
check() {   # check <n> <what> <host grep args...>
    local n=$1 what=$2; shift 2
    mcopy -n -i "$WORK/disk.img" "::/O$n.TXT" "$WORK/got$n" 2>/dev/null
    LC_ALL=C grep -F "$@" > "$WORK/want$n"
    if cmp -s "$WORK/got$n" "$WORK/want$n"; then echo "  ok    $what matches grep -F"
    else echo "  FAIL  $what differs from grep -F"; fail=1; fi
}
check 1 "a plain match (long line, no final newline)" soup "$G"
check 2 "-v"                                          -v soup "$G"
check 3 "-c"                                          -c soup "$G"
check 4 "-i"                                          -i soup "$G"
check 5 "-c -i"                                       -c -i soup "$G"
check 6 "-v -i"                                       -v -i soup "$G"
check 7 "sift.elf as a pipe stage"                    soup "$G"
check 8 "-n"                                          -n soup "$G"
check 9 "-o (two matches on one line)"                -o soup "$G"
check 10 "-o -i (the matches as written)"             -o -i soup "$G"
check 11 "-noi"                                       -noi soup "$G"
check 12 "-o -v (prints nothing)"                     -o -v soup "$G"
check 13 "-o -c (counts lines)"                       -o -c soup "$G"
check 14 "-n -c -i"                                   -n -c -i soup "$G"
tr -d '\r' < "$WORK/serial.log" | grep "/sift.elf exited with code 1" >/dev/null \
    && echo "  ok    no match exits 1, as grep does" || { echo "  FAIL  exit code for no match"; fail=1; }
exit $fail

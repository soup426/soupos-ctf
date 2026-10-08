#!/usr/bin/env bash
# chain-test.sh - ; && and || in the shell, against sh (v0.57.0).
#
# Each case is a command line whose parts leave marker files when they run.
# The same line runs in the host's sh, with (exit 42) standing in for
# greet.elf (which exits 42), (exit 127) for an unknown command, and the
# same echo > file for call.elf. The marker files left must be the same.
# (Case 8's host wc reads a file: GNU wc pads a pipe to 7, weigh.elf cannot
# tell a pipe from a file; see weigh.c.)
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-chain.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# soupOS line # host sh line (files are written as /Cn*.TXT in both)
CASES=(
  "cook call.elf a > /C1A.TXT ; cook call.elf b > /C1B.TXT#echo a > C1A.TXT ; echo b > C1B.TXT"
  "cook call.elf a > /C2A.TXT && cook call.elf b > /C2B.TXT#echo a > C2A.TXT && echo b > C2B.TXT"
  "cook greet.elf && cook call.elf b > /C3B.TXT#(exit 42) && echo b > C3B.TXT"
  "cook greet.elf || cook call.elf b > /C4B.TXT#(exit 42) || echo b > C4B.TXT"
  "cook call.elf a > /C5A.TXT || cook call.elf b > /C5B.TXT#echo a > C5A.TXT || echo b > C5B.TXT"
  "cook greet.elf && cook call.elf b > /C6B.TXT || cook call.elf c > /C6C.TXT#(exit 42) && echo b > C6B.TXT || echo c > C6C.TXT"
  "nosuchthing && cook call.elf b > /C7B.TXT ; cook call.elf c > /C7C.TXT#(exit 127) && echo b > C7B.TXT ; echo c > C7C.TXT"
  "cook call.elf x y | weigh.elf > /C8A.TXT && cook call.elf ok > /C8B.TXT#echo x y > t8 && wc < t8 > C8A.TXT && echo ok > C8B.TXT"
  "cook greet.elf ; cook greet.elf || cook call.elf a > /C9A.TXT && cook call.elf b > /C9B.TXT#(exit 42) ; (exit 42) || echo a > C9A.TXT && echo b > C9B.TXT"
)
keys=("headchef" "rosemary" "WAIT:1")
for c in "${CASES[@]}"; do keys+=("${c%%#*}" "WAIT:3"); done
# The ordinary cook: a refused builtin is a failure (status 1).
keys+=("clockout" "WAIT:1" "cook" "soup" "WAIT:2"
       "reheat && cook call.elf b > /home/cook/D1.TXT" "WAIT:2"
       "pour /etc/kitchen || cook call.elf r > /home/cook/D2.TXT" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"
for c in "${CASES[@]}"; do ( cd "$WORK/host" && sh -c "${c#*#}" >/dev/null 2>&1 ); done
got=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null | grep -oE "C[0-9]+[A-Z]\.TXT" | sort | tr '\n' ' ')
want=$(cd "$WORK/host" && ls C*.TXT | sort | tr '\n' ' ')
[ "$got" = "$want" ] && echo "  ok    nine chains left the same files as sh: $want" \
    || { echo "  FAIL  chains: soupOS [$got]  sh [$want]"; fail=1; }
H=$(mdir -b -i "$WORK/disk.img" ::/home/cook 2>/dev/null)
! echo "$H" | grep "D1.TXT" >/dev/null && echo "$H" | grep "D2.TXT" >/dev/null \
    && echo "  ok    a refused builtin is a failure: && skips after it, || runs" || { echo "  FAIL  a refusal's status"; fail=1; }
mcopy -n -i "$WORK/disk.img" ::/C8A.TXT "$WORK/c8a" 2>/dev/null
[ "$(cat "$WORK/c8a" 2>/dev/null)" = "$(cat "$WORK/host/C8A.TXT")" ] \
    && echo "  ok    a pipe inside a chain is still a pipe" || { echo "  FAIL  the pipeline in case 8"; fail=1; }
exit $fail

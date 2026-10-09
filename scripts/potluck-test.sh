#!/usr/bin/env bash
# potluck-test.sh - potluck.elf (comm) against the host's comm (v0.60.111):
# three columns, -1 -2 -3 and -12 -13 -23 -123, repeated lines, an empty
# file, a last line with no newline, and stdin as -.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-potluck.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'apple\nbasil\nbasil\ncarrot\nleek\nonion\nzest\n' > "$H/a.txt"
printf 'Apple\nbasil\ncarrot\ncarrot\nfennel\nonion\nthyme\n' > "$H/b.txt"
: > "$H/e.txt"; printf 'basil\nleek' > "$H/n.txt"
for f in a.txt b.txt e.txt n.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
CASES=("" "-1" "-2" "-3" "-12" "-13" "-23" "-123" "-1 -3")
keys=("headchef" "rosemary" "WAIT:1"); k=0
for o in "${CASES[@]}"; do k=$((k+1)); keys+=("cook potluck.elf $o /a.txt /b.txt > /o$k.txt" "UNTIL:@soupOS:"); done
keys+=("cook potluck.elf /a.txt /e.txt > /oe1.txt" "UNTIL:@soupOS:" "cook potluck.elf /e.txt /b.txt > /oe2.txt" "UNTIL:@soupOS:"
       "cook potluck.elf /n.txt /a.txt > /on.txt" "UNTIL:@soupOS:"
       "cook spoon.elf /b.txt | cook potluck.elf /a.txt - > /os.txt" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
bad=0
cmp_() {  # cmp_ NAME WANT-COMMAND...
    local n=$1; shift
    if ! mcopy -n -i "$WORK/disk.img" "::/$n" "$WORK/g.$n" 2>/dev/null; then echo "        $n was not written"; bad=1; return; fi
    (cd "$H" && LC_ALL=C "$@") > "$WORK/w.$n" 2>/dev/null
    cmp -s "$WORK/w.$n" "$WORK/g.$n" || { echo "        $n ($*) differs:"; diff "$WORK/w.$n" "$WORK/g.$n" | sed 's/^/          /'; bad=1; }
}
k=0; for o in "${CASES[@]}"; do k=$((k+1)); cmp_ o$k.txt comm $o a.txt b.txt; done
cmp_ oe1.txt comm a.txt e.txt; cmp_ oe2.txt comm e.txt b.txt
cmp_ on.txt comm --nocheck-order n.txt a.txt
cmp_ os.txt sh -c 'comm a.txt - < b.txt'
[ "$bad" = 0 ] && echo "  ok    potluck is the host's comm (9 column choices, repeats, empty files, no last newline, stdin as -)" \
    || { echo "  FAIL  potluck differs from comm"; fail=1; }
exit $fail

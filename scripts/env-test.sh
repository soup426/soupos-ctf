#!/usr/bin/env bash
# env-test.sh - hand (export) and a program's environment, against bash (v0.60.26).
#
# The same lines run in soupOS and in bash, with hand as export, discard as
# unset, `labels.elf | rack.elf` as `env | grep ^S | sort`, `labels.elf X`
# as printenv X, raise.elf as tr a-z A-Z; every file written must match.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-env.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'SA=1 ; SB=2 ; hand SA ; cook labels.elf | rack.elf > /E1.TXT'
  'hand SB SC=three ; cook labels.elf | rack.elf > /E2.TXT'
  'discard SA ; cook labels.elf | rack.elf > /E3.TXT'
  'hand SD ; cook labels.elf | rack.elf > /E4.TXT ; SD=late ; cook labels.elf | rack.elf > /E5.TXT'
  'SB=changed ; cook labels.elf SB > /E6.TXT'
  'cook labels.elf NOPE > /E7.TXT ; cook call.elf $? > /E8.TXT'
  'hand SE="a b" ; cook labels.elf SE > /E9.TXT'
  'cook labels.elf SC | raise.elf > /E10.TXT'
)
FILES="E1 E2 E3 E4 E5 E6 E7 E8 E9 E10"
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("cook mise.elf after-env" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook labels.elf | rack.elf/env | grep -E \'^S[A-E]=\' | sort}
    b=${b//cook labels.elf/printenv}; b=${b//hand /export }; b=${b//discard /unset }
    b=${b//cook call.elf/echo}; b=${b//raise.elf/tr a-z A-Z}; b=${b//> \//> }
    script+="$b"$'\n'
done
( cd "$WORK/host" && env -i PATH="$PATH" LC_ALL=C bash -c "$script" ) >/dev/null 2>&1
bad=0
for f in $FILES; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" || { echo "        $f.TXT: got '$(cat "$WORK/got.$f")', bash '$(cat "$WORK/host/$f.TXT")'"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    all ${#LINES[@]} lines write what bash writes ($(echo $FILES | wc -w) files: handed and not, discard, hand before set, a changed value, a missing name's status, a quoted value, both stages of a pipe)" \
    || { echo "  FAIL  hand differs from bash's export"; fail=1; }
grep -q 'TESTOUT args=after-env' "$WORK/serial.log" && echo "  ok    programs still run after all that" \
    || { echo "  FAIL  a program did not run after the env lines"; fail=1; }
exit $fail

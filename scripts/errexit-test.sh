#!/usr/bin/env bash
# errexit-test.sh - follow -e against bash -e (v0.60.57).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-errexit.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# one script, bash spelling; the soupOS copy is made from it by substitution
cat > "$WORK/a.sh" <<'S'
echo E1:start
if false ; then echo E2:no ; fi
while false ; do echo E3:no ; done
false || echo E4:rescued
false && echo E5:no
echo E6:after-lists
for i in 1 2 ; do echo E7:$i ; done
f() { echo E8:in-f ; false ; echo E9:no ; }
f
echo E10:no
S
cat > "$WORK/b.sh" <<'S'
echo B1:start
x=1
until test $x -gt 2 ; do echo B2:$x ; x=$((x+1)) ; done
test 1 -eq 2
echo B3:no
S
to_soup() { sed -e 's/\becho\b/slurp/g' -e 's/\bfalse\b/cook taste.elf 1 -eq 2/g' -e 's/\btest\b/cook taste.elf/g' "$1"; }
to_soup "$WORK/a.sh" > "$WORK/A.SH"; to_soup "$WORK/b.sh" > "$WORK/B.SH"
mcopy -i "$WORK/disk.img" "$WORK/A.SH" ::/a.sh; mcopy -i "$WORK/disk.img" "$WORK/B.SH" ::/b.sh
LINES=(
  'follow -e /a.sh ; slurp E11:status:$?'
  'follow -e /b.sh ; slurp B4:status:$?'
  'follow /b.sh ; slurp B5:status:$?'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[EB][0-9]+:' > "$WORK/got"
{ bash -e "$WORK/a.sh"; echo "E11:status:$?"
  bash -e "$WORK/b.sh"; echo "B4:status:$?"
  bash "$WORK/b.sh"; echo "B5:status:$?"; } 2>/dev/null | grep -E '^[EB][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    follow -e stops where bash -e stops ($(wc -l < "$WORK/want") lines: if/while/until conditions, && and ||, a loop, a failure in a function, its status, and no -e runs on)"
else echo "  FAIL  follow -e differs from bash -e:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

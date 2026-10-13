#!/usr/bin/env bash
# soupyc-include-test.sh - soupyc's include finds its file (v0.60.272,
# review R-K20/R-T25). include copied the name from tok.sval, but the lexer
# keeps a string literal in tok.str, so the name was always empty and every
# include said "file not found": the disk's own INCLUDE.SC never ran, and
# soupyc-perms-test's include case could not fail. Here the headchef runs
# INCLUDE.SC (cube(3) from MATHLIB.SC is 27); saucier includes /MATHLIB.SC
# (cube(2) is 8) and is refused /etc/kitchen, none of it printed.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-scinclude.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "soup INCLUDE.SC" "WAIT:3" "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" \
    'soup -c include "/MATHLIB.SC" pour "CU" + "BE " + cube(2)' "UNTIL:CUBE " "WAIT:1" \
    'soup -c include "/etc/kitchen"' "WAIT:2" "slurp INC-DONE" "UNTIL:INC-DONE" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
grep 'cube(3)  = 27' "$WORK/l" >/dev/null && echo "  ok    INCLUDE.SC ran: cube(3) from MATHLIB.SC is 27" \
    || { echo "  FAIL  INCLUDE.SC: $(grep -E 'include|cube' "$WORK/l" | grep -v 'soup ' | head -2 | tr '\n' ';')"; fail=1; }
grep '^CUBE 8' "$WORK/l" >/dev/null && echo "  ok    saucier's include of /MATHLIB.SC works" \
    || { echo "  FAIL  saucier's include: $(grep -o 'CUBE.*\|include:.*' "$WORK/l" | head -2 | tr '\n' ';')"; fail=1; }
grep 'headchef:0:' "$WORK/l" >/dev/null && { echo "  FAIL  saucier's include printed /etc/kitchen"; fail=1; }
[ "$(grep -c 'include: file not found' "$WORK/l")" = 1 ] && echo "  ok    saucier's include of /etc/kitchen is refused, nothing of it shown" \
    || { echo "  FAIL  the refusals: $(grep -c 'include: file not found' "$WORK/l") (want 1, the kitchen's)"; fail=1; }
[ "$fail" = 0 ] && echo "soupyc-include-test: PASS" || echo "soupyc-include-test: FAIL"
exit "$fail"

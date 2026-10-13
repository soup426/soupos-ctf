#!/usr/bin/env bash
# soupyc-perms-test.sh - soupyc's files obey the cook's rights (v0.60.262,
# review R-K12, critical). soupyc runs in ring 0 on the shell's task, and its
# open(), lines(), write_lines() and include never asked users_may: any cook
# read /etc/kitchen with `soup -c` (the headchef's hash, where pour said
# Permission denied) and wrote anywhere. As saucier each of them is tried on
# /etc/kitchen or in /, and each must be refused; saucier's own file still
# reads. The CHALLENGE build keeps the unchecked open: it is that build's
# stage 1. (v0.60.280, review R-K21) remove() was missed: saucier deleted
# /etc/kitchen, and the next boot reseeded the headchef's stock secret; it
# is refused now, and saucier's own file still removes.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-scperms.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" \
    "clockout" "WAIT:2" "saucier" "basil" "WAIT:2" "stir mine.txt ownline" "WAIT:1" \
    'soup -c let f = open("/etc/kitchen") pour "OPEN " + f' "UNTIL:OPEN " \
    'soup -c let l = lines("/etc/kitchen") pour "LINES " + len(l)' "UNTIL:LINES " \
    'soup -c let w = open("/SCPERM.TXT", "w") pour "WOPEN " + w' "UNTIL:WOPEN " \
    'soup -c pour "WLINES " + write_lines("/SCLINES.TXT", ["x"])' "UNTIL:WLINES " \
    'soup -c include "/etc/kitchen"' "WAIT:2" \
    'soup -c let g = open("/home/saucier/mine.txt") pour "MINE " + read(g, 20)' "UNTIL:MINE " "WAIT:1" \
    "stir gone.txt bye" "WAIT:1" \
    'soup -c pour "RM" + "K " + remove("/etc/kitchen")' "UNTIL:RMK " \
    'soup -c pour "RM" + "O " + remove("/home/saucier/gone.txt")' "UNTIL:RMO " "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU: $(tr -d '\r' < "$WORK/log" | tail -2 | tr '\n' ' ')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
tr -d '\r' < "$WORK/log" > "$WORK/l"
want() { grep -q "$1" "$WORK/l" && echo "  ok    $2" || { echo "  FAIL  $2: $(grep -o "^${3:-$1}.*" "$WORK/l" | head -1)"; fail=1; }; }
want 'OPEN -1' "open(\"/etc/kitchen\") refused to saucier" 'OPEN '
want 'LINES 0' "lines(\"/etc/kitchen\") gives saucier nothing" 'LINES '
want 'WOPEN -1' "open(\"/SCPERM.TXT\", \"w\") in / refused" 'WOPEN '
want 'WLINES -1' "write_lines in / refused" 'WLINES '
want 'MINE ownline' "saucier's own file still reads" 'MINE '
want 'RMK 0' "remove(\"/etc/kitchen\") refused to saucier (v0.60.280, review R-K21)" 'RMK '
want 'RMO 1' "saucier's own file still removes" 'RMO '
grep -q 'headchef:0:' "$WORK/l" && { echo "  FAIL  the headchef's line of /etc/kitchen was printed"; fail=1; }
mdir -i "$WORK/disk.img" ::/etc/kitchen >/dev/null 2>&1 || { echo "  FAIL  /etc/kitchen is gone from the disk"; fail=1; }
mdir -i "$WORK/disk.img" ::/home/saucier/gone.txt >/dev/null 2>&1 && { echo "  FAIL  saucier's gone.txt is still there"; fail=1; }
for f in SCPERM.TXT SCLINES.TXT; do
    mdir -i "$WORK/disk.img" "::/$f" >/dev/null 2>&1 && { echo "  FAIL  /$f was made in /"; fail=1; }
done
[ "$fail" = 0 ] && echo "soupyc-perms-test: PASS" || echo "soupyc-perms-test: FAIL"
exit "$fail"

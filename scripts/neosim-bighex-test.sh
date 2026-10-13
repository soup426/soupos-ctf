#!/usr/bin/env bash
# neosim-bighex-test.sh - Ctrl-A on a long hex number (v0.60.289, review
# R-N31). The number was rebuilt in char out[40], zero-padded to as many
# digits as it had (up to 253): a 0x with 61 digits wrote past it, and
# neosim went down with its unsaved work. Here 0x, 60 zeros and a 1; Ctrl-A
# and :w: the file holds 0x, 60 zeros and a 2, the digits kept.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-neobighex.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; [ "${KEEP:-0}" = 1 ] && echo "  kept $WORK" || rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
Z=$(python3 -c "print('0' * 60)")
printf '0x%s1\n' "$Z" > "$WORK/h"; mcopy -o -i "$WORK/disk.img" "$WORK/h" ::HEX.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
timeout 120 python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "cook neosim.elf -wait /HEX.TXT &" "WAIT:1" "countertop" \
    "UNTIL:[neosim] window up" "WAIT:1" "KEY:ctrl-a" "WAIT:1" "TYPE::w" "KEY:ret" "UNTIL:[neosim] wrote /HEX.TXT" "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  neosim did not live to write: $(tr -d '\r' < "$WORK/log" | grep -E '\[neosim\]|fault|killed' | tail -3 | tr '\n' ';')"; fail=1; }
kill $PID 2>/dev/null; wait $PID 2>/dev/null; PID=""
got=$(mcopy -n -i "$WORK/disk.img" ::HEX.TXT - 2>/dev/null)
[ "$got" = "0x${Z}2" ] && echo "  ok    Ctrl-A on 0x and 61 digits gives the same 61 digits, the last one 2" \
    || { echo "  FAIL  HEX.TXT: $got"; fail=1; }
[ "$fail" = 0 ] && echo "neosim-bighex-test: PASS" || echo "neosim-bighex-test: FAIL"
exit "$fail"

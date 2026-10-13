#!/usr/bin/env bash
# kbdtap-test.sh - soupyc may not open /dev/kbd (v0.60.264, review R-K13).
# v0.60.257 sent a program's reads through the stdin rule, but soupyc's
# open("/dev/kbd") still reached dev_kbd_read with no foreground rule, and a
# spawn()ed soupyc task outlives a shift: the next cook's keys. soupyc's open
# of it gives -1 now. (Its SSH half went in v0.60.271, review R-T24: the
# shell's pour never reaches the device, so it passed whatever the kernel
# did; dev_kbd_read's other-terminal guard stays as a backstop, untested.)
set -uo pipefail
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-kbdtap.XXXXXX)
PID=""
trap '[ -n "$PID" ] && kill $PID 2>/dev/null; rm -rf "$WORK"' EXIT
fail=0
cp disk.img "$WORK/disk.img"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    'soup -c let k = open("/dev/kbd") pour "KBD " + k' "UNTIL:KBD " "WAIT:1" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
tr -d '\r' < "$WORK/log" | grep '^KBD -1' >/dev/null && echo "  ok    soupyc's open(\"/dev/kbd\") is refused" \
    || { echo "  FAIL  soupyc opened /dev/kbd: $(tr -d '\r' < "$WORK/log" | grep -o '^KBD .*' | head -1)"; fail=1; }
[ "$fail" = 0 ] && echo "kbdtap-test: PASS" || echo "kbdtap-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# Does the entropy pool actually vary between boots?
#
# `sample` checks the output looks balanced, but a pool seeded from nothing
# would pass that too: SHA-256 of a constant is just as balanced as SHA-256 of
# a secret. The property that matters is that two boots draw different bytes,
# so this boots twice and compares the draw each `sample` logs.
set -uo pipefail
cd "$(dirname "$0")/.."

draw() {
    local log mon disk pid
    log=$(mktemp /tmp/soupos-rnd.XXXXXX.log)
    mon=$(mktemp -u /tmp/soupos-rnd.XXXXXX.sock)
    disk=$(mktemp /tmp/soupos-rnddisk.XXXXXX.img)
    cp disk.img "$disk"
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$log" -monitor "unix:$mon,server,nowait" >/dev/null 2>&1 &
    pid=$!
    sleep 4
    python3 scripts/qemu_keys.py "$mon" "headchef" "rosemary" "WAIT:1" "sample" >/dev/null 2>&1
    # Until sample's own last line, not a fixed 8 s (2026-10-08).
    for i in $(seq 1 120); do grep -qE "^\[sample\] [0-9]+ tests, [0-9]+ failed" "$log" && break; sleep 0.5; done
    kill "$pid" 2>/dev/null
    grep -oE "^\[sample\] random draw: [0-9a-f]{16}" "$log" | awk '{print $4}'
    rm -f "$log" "$mon" "$disk"
}

a=$(draw); b=$(draw)
fail=0
[ ${#a} -eq 16 ] && [ ${#b} -eq 16 ] \
    && echo "  ok    both boots logged a draw ($a, $b)" \
    || { echo "  FAIL  a boot logged no draw ('$a', '$b')"; fail=1; }
[ -n "$a" ] && [ "$a" != "$b" ] \
    && echo "  ok    two boots drew different bytes" \
    || { echo "  FAIL  two boots drew the same bytes"; fail=1; }
exit $fail

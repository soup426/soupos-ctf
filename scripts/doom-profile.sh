#!/usr/bin/env bash
# What does a Doom frame cost, in each display mode?
#
# Reaching these numbers took longer than reading them, so the recipe is here:
#
#   - PROFILE=1 makes Doom report to the serial log every 50 FRAMES, and only
#     the in-level render loop counts frames. The title screen and the menus
#     report nothing, so a run that never starts a level looks broken.
#   - Getting into a level headlessly needs THREE returns: main menu ->
#     episode -> skill. Two is not enough, which is what made this look
#     impossible the first time.
#   - fps is capped by FRAME_TICKS (2 ticks of a 100 Hz timer = 50 fps), so a
#     reading of exactly 50 means "the cap", not "the ceiling". To find the
#     ceiling, set FRAME_TICKS to 0 and rebuild: it measured 1666 fps on the
#     framebuffer build, so the cap uses about 3% of what the machine can do.
#
# Figures are thousands of cycles per frame. `copy` is the handover to the
# display: a 64 KB memcpy in mode 13h, a 2.3 MB scale-and-blit on the
# framebuffer.
set -uo pipefail
cd "$(dirname "$0")/.."

[ -f DOOM1.WAD ] || { echo "  skip  no DOOM1.WAD"; exit 0; }

WORK=$(mktemp -d)
PIDS=""
cleanup() { for p in $PIDS; do kill "$p" 2>/dev/null; done; rm -rf "$WORK"; }
trap cleanup EXIT

run() {   # run <label> <make-flags...>
    local label=$1; shift
    make clean >/dev/null 2>&1
    make "$@" PROFILE=1 iso >/dev/null 2>&1 || { echo "  FAIL  build ($label)"; return 1; }

    local disk=$WORK/$label.img log=$WORK/$label.log mon=$WORK/$label.sock
    cp disk.img "$disk"
    qemu-system-i386 -accel kvm -cpu host \
        -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d \
        -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$log" -monitor "unix:$mon,server,nowait" >/dev/null 2>&1 &
    local pid=$!; PIDS="$PIDS $pid"
    sleep 5
    python3 scripts/qemu_keys.py "$mon" "headchef" "rosemary" "WAIT:1" \
        "doom" "WAIT:6" "KEY:ret" "WAIT:2" "KEY:ret" "WAIT:2" "KEY:ret" \
        "WAIT:25" >/dev/null 2>&1
    kill "$pid" 2>/dev/null
    local line
    line=$(grep "doomprof" "$log" | tail -1)
    if [ -z "$line" ]; then
        echo "  FAIL  $label: the profiler never reported (did it reach a level?)"
        return 1
    fi
    printf '  %-14s %s\n' "$label" "${line#*] }"
}

run framebuffer FB=1 || exit 1
run mode13h     FB=0 || exit 1

make clean >/dev/null 2>&1
make >/dev/null 2>&1
echo "  (both capped at 50 fps by FRAME_TICKS; the difference is in 'copy')"

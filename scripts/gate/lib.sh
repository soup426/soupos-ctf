#!/usr/bin/env bash
# Shared by every gate segment under scripts/gate/. A segment is one boot of
# soupOS with no display, a drive sequence typed through the QEMU monitor, and
# a set of greps over the COM1 serial log; this file is the boot, the typing,
# the grep and the verdict, so a segment is nothing but its own sequence and
# its own assertions.
#
# The gate used to be ONE boot and one five-minute sequence (smoke-test.sh up
# to v0.30.1). Its assertions were written against that single log, and some
# count lines across it: two IRQ kills, three `killed` lines, the last two
# greet.elf exits. Those still hold, because the split keeps every line a
# count depends on inside the segment that counts it. When adding a check that
# counts, keep it that way.
#
#   source "$(dirname "$0")/lib.sh"
#   gate_init <name> [net]        # net: start the 19000-19002 host listeners
#   gate_boot                     # copy of the disk, headless QEMU, 4 s
#   gate_drive "<cmd>" "WAIT:n" ... (logs in as headchef first)
#   check "<description>" [-cN] '<grep -E pattern>'
#   gate_finish                   # prints the verdict, exits non-zero on FAIL
#
# KEEP=1 keeps the serial log and says where it is.
set -uo pipefail

GATE_NAME=""
LOG=""; MON=""; WAV=""; TESTDISK=""; QEMU_PID=""; LISTENER_PIDS=""
fail=0

gate_cleanup() {
    [ -n "$LISTENER_PIDS" ] && kill $LISTENER_PIDS 2>/dev/null
    [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null
    rm -f "$MON" "$TESTDISK" "$WAV"
    # A failing segment always keeps its log: a rare failure that leaves no
    # evidence has to wait for its next occurrence to be understood (one did,
    # 2026-10-07: "> redirection reached the filesystem", once in ~23 runs).
    if [ "${KEEP:-0}" = "1" ] || [ "${fail:-0}" != "0" ]; then echo "  [$GATE_NAME] serial log kept at $LOG"; else rm -f "$LOG"; fi
}

gate_init() {
    GATE_NAME=$1
    cd "$(dirname "${BASH_SOURCE[0]}")/../.."
    LOG=$(mktemp "/tmp/soupos-gate-$GATE_NAME.XXXXXX.log")
    MON=$(mktemp -u "/tmp/soupos-gate-$GATE_NAME.XXXXXX.sock")
    WAV=$(mktemp -u "/tmp/soupos-gate-$GATE_NAME.XXXXXX.wav")
    trap gate_cleanup EXIT
    [ -f soupOS.iso ] || { echo "  FAIL  soupOS.iso missing - run 'make' first"; exit 1; }
    [ -f disk.img ]   || { echo "  FAIL  disk.img missing - run 'make disk' first"; exit 1; }

    if [ "${2:-}" = "net" ]; then
        # Something for the TCP checks to connect to: QEMU's user networking
        # maps the guest's 10.0.2.2 to the host's loopback. One port per check,
        # deliberately - three connections in a row to the SAME SLIRP port
        # leave the third unanswered, QEMU-side state that outlives our close.
        for port in 19000 19001 19002; do
            python3 scripts/gate/listener.py "$port" >/dev/null 2>&1 &
            LISTENER_PIDS="$LISTENER_PIDS $!"
        done
    fi
}

gate_boot() {
    # A throwaway copy of the disk: an interactive QEMU window holding
    # disk.img must not block the tests (the image is write-locked), and a
    # hermetic disk means no state leaks between runs or between segments.
    TESTDISK=$(mktemp "/tmp/soupos-gate-$GATE_NAME.XXXXXX.img")
    cp disk.img "$TESTDISK"
    qemu-system-i386 \
        -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d \
        -m 128M -no-reboot -no-shutdown \
        -netdev user,id=n0 -device rtl8139,netdev=n0 \
        -audiodev wav,id=a0,path="$WAV" -device AC97,audiodev=a0 \
        -display none \
        -serial "file:$LOG" \
        -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
    QEMU_PID=$!
    sleep 4
}

gate_drive() {
    python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" "$@" >/dev/null || {
        echo "  FAIL  [$GATE_NAME] could not drive the QEMU monitor"; fail=1; }
}

check() {   # check <description> [-cN] <grep-pattern>
    # -cN asserts the pattern appears at least N times, which is how a repeat
    # of the same output (two opens of one file) gets distinguished from one.
    if [ "${2#-c}" != "$2" ]; then
        local want=${2#-c} got
        got=$(grep -cE "$3" "$LOG")
        if [ "$got" -ge "$want" ]; then echo "  ok    $1"
        else echo "  FAIL  $1  (saw $got of $want for /$3/)"; fail=1; fi
        return
    fi
    if grep -qE "$2" "$LOG"; then echo "  ok    $1"
    else echo "  FAIL  $1  (no match for /$2/)"; fail=1; fi
}

# Line number of the first (or last, with "last") match, empty if none.
line_of() { if [ "${2:-}" = last ]; then grep -nE "$1" "$LOG" | tail -1 | cut -d: -f1; else grep -nE "$1" "$LOG" | head -1 | cut -d: -f1; fi; }
# Lines strictly between two line numbers.
between() { awk -v a="$1" -v b="$2" 'NR>a && NR<b' "$LOG"; }
after()   { awk -v a="$1" 'NR>a' "$LOG"; }

gate_finish() {
    # Every segment asserts this, because a panic anywhere is a failed gate.
    if grep -qE 'KERNEL PANIC' "$LOG"; then
        echo "  FAIL  kernel panicked:"; grep -A6 'KERNEL PANIC' "$LOG" | sed 's/^/        /'; fail=1
    else
        echo "  ok    no kernel panic [$GATE_NAME]"
    fi
    if [ "$fail" = 0 ]; then echo "GATE SEGMENT $GATE_NAME PASSED"; else echo "GATE SEGMENT $GATE_NAME FAILED"; fi
    exit $fail
}

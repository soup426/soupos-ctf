#!/usr/bin/env bash
# gate-patterns-test.sh - the gate's own checks must fail on failure (v0.60.251).
# Each case names a check by its description in a scripts/gate/ segment and
# a serial log; that check line, taken from the segment as written, runs
# against the log. case_ gives it the failure it is there to catch and it
# must say FAIL; ok_ gives it the real success line and it must pass. No
# QEMU: a pattern that matches its own failure is the bug looked for.
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-gatepat.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
fail=0
source scripts/gate/lib.sh >/dev/null 2>&1
set +u
case_() { want=FAIL; run_ "$@"; }
ok_()   { want=ok; run_ "$@"; }
run_() {
    local seg=$1 desc=$2; shift 2
    local line
    line=$(grep -F "check \"$desc\"" "scripts/gate/$seg.sh" | head -1)
    [ -n "$line" ] || { echo "        no check \"$desc\" in $seg.sh"; fail=1; return; }
    LOG="$WORK/log"; printf '%s\n' "$@" > "$LOG"
    local out; out=$(fail=0; eval "$line")
    case "$want:$out" in
        FAIL:*FAIL*|ok:*"  ok "*) ;;
        FAIL:*) echo "        $seg: \"$desc\" passes on: $*"; fail=1 ;;
        *) echo "        $seg: \"$desc\" fails on: $*"; fail=1 ;;
    esac
}
# R-T1: preempttest's own verdict, not any PASS in the log
case_ boot "preempt_disable works" "[pt] A=1 B=2 C=3 depth=0" "[pt] FAIL" "[vmtest] PASS"
ok_   boot "preempt_disable works" "[pt] A=1 B=2 C=3 depth=0" "[pt] PASS"
# R-T4: mise's heap round trip, not a heap line that says FAIL
case_ boot "ring3 sbrk syscall" "TESTOUT heap=FAIL"
ok_   boot "ring3 sbrk syscall" "TESTOUT heap=AX"
# R-T4: drop.elf killed by its page fault (-(128 + 14)), not a clean exit
case_ boot "a wild fault still kills" "crash: STILL ALIVE (this should not print)" "[proc 15] /drop.elf exited with code 0, 29634 pages free, 1 ticks"
ok_   boot "a wild fault still kills" "[proc 15] /drop.elf exited with code -142, 29634 pages free, 1 ticks"
[ "$fail" = 0 ] && echo "gate-patterns-test: PASS" || echo "gate-patterns-test: FAIL"
exit "$fail"

#!/usr/bin/env bash
# The gate: every segment under scripts/gate/ booted at once, one verdict.
#
# Until v0.30.1 this was one boot driven through a five-minute sequence
# (smoke-test.sh). The segments are that sequence cut where nothing on one
# side counts lines from the other, so each boots its own QEMU and the gate
# takes as long as its slowest segment instead of the sum. Run one segment on
# its own while working on it:  scripts/gate/jobs.sh
#
#   scripts/gate.sh              all segments
#   KEEP=1 scripts/gate.sh       keep every segment's serial log
set -uo pipefail
cd "$(dirname "$0")/.."
WORK=$(mktemp -d /tmp/soupos-gate.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
T0=$(date +%s)

segs=(boot jobs fat vga desk net soupyc audio)
for s in "${segs[@]}"; do
    ( t=$(date +%s); ./scripts/gate/$s.sh >"$WORK/$s.out" 2>&1; echo "$? $(( $(date +%s) - t ))" >"$WORK/$s.status" ) &
done
wait

fail=0
for s in "${segs[@]}"; do
    read -r code secs <"$WORK/$s.status"
    echo "── $s (${secs}s) ──"
    grep -E '^  (ok|FAIL)' "$WORK/$s.out" | grep -vE '^  ok    no kernel panic'
    grep -qE '^  ok    no kernel panic' "$WORK/$s.out" || grep -E 'panicked|could not drive' "$WORK/$s.out"
    [ "$code" = 0 ] || fail=1
done
echo "  ok    no kernel panic in any of ${#segs[@]} segments" ; grep -lE 'KERNEL PANIC' "$WORK"/*.out >/dev/null 2>&1 && fail=1
echo
echo "gate: $(( $(date +%s) - T0 ))s, $(cat "$WORK"/*.out | grep -cE '^  ok') ok, $(cat "$WORK"/*.out | grep -cE '^  FAIL') failed"
if [ "$fail" = 0 ]; then echo "SMOKE TEST PASSED"; else echo "SMOKE TEST FAILED"; fi
exit $fail

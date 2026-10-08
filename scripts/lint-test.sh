#!/usr/bin/env bash
# lint-test.sh - no test may pipe into grep -q (v0.60.20).
#
# Every test runs under `set -o pipefail` (the gate's segments get it from
# lib.sh). grep -q exits at its first match; whatever was still writing into
# the pipe then dies of SIGPIPE, and pipefail makes the whole pipeline fail:
# a check that found what it wanted reports that it did not, and a negated
# one (`! ... | grep -q`) passes when it should fail. Measured 2026-10-08 on
# the gate's own jobs check: 39 false failures in 2000 runs on an idle host.
# Pipe into `grep ... >/dev/null` instead, which reads to the end.
set -uo pipefail
cd "$(dirname "$0")/.."
hits=$(grep -nE '\|[[:space:]]*grep[[:space:]]+-[A-Za-z]*q' scripts/*.sh scripts/gate/*.sh | grep -v '^scripts/lint-test.sh:')
if [ -z "$hits" ]; then echo "  ok    no test pipes into grep -q"
else echo "  FAIL  a test pipes into grep -q (use grep ... >/dev/null):"; echo "$hits" | sed 's/^/        /'; exit 1; fi

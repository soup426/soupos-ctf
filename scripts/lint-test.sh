#!/usr/bin/env bash
# lint-test.sh - no test may pipe into grep -q (v0.60.20), and no test
# may share a name in check.sh (v0.60.82).
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

# Every script check.sh runs, once (v0.60.82). Three new tests had been
# written over old ones of the same name: subst-test.sh ($(COMMAND), v0.60.3)
# by v0.60.65's, random-test.sh (the entropy pool across boots) by
# v0.60.58's, and smoke-test.sh, the gate itself, by v0.60.82's draft. Each
# left its name in check.sh twice, so a name run twice is the sign.
names=$(awk '/^run "/ && /\.\/scripts\// { match($0, /scripts\/[a-z0-9-]+\.sh/); print substr($0, RSTART + 8, RLENGTH - 11) }
             /for t in/,/; do/ { for (i = 1; i <= NF; i++) { f = $i; sub(/;$/, "", f); if (f ~ /^[a-z0-9-]+-test$/) print f } }' scripts/check.sh)
dups=$(echo "$names" | sort | uniq -d | tr '\n' ' ')
missing=""; for n in $names; do [ -f "scripts/$n.sh" ] || missing+="$n "; done
if [ -z "$dups$missing" ]; then echo "  ok    check.sh runs $(echo "$names" | wc -l) scripts, each name once and each there"
else echo "  FAIL  check.sh names twice: ${dups:-none}; names with no script: ${missing:-none}"; exit 1; fi

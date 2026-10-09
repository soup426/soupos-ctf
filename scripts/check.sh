#!/usr/bin/env bash
# Everything that must pass before a commit, run at once instead of in a row.
#
# The pieces were always independent: the five configurations only need a
# compiler each, and every test script boots its own QEMU from a copy of the
# disk. What serialised them was the tree itself - `make clean` for the next
# configuration deleted the soupOS.iso the gate was booting from - so the
# other configurations now build in throwaway copies of the tree while the
# default build here feeds the gate and the side scripts in parallel. On a
# 16-core laptop that turns ten minutes into the length of the slowest piece.
#
# Usage: scripts/check.sh            full set
#        scripts/check.sh quick      configs + gate only (no side scripts)
#        KEEP=1 scripts/check.sh     keep the logs under /tmp/soupos-check.*
#        CHECK_JOBS=8 scripts/check.sh   at most 8 pieces at a time
#        scripts/remote-check.sh     all of it on the test container, not here
#
# Exit status is non-zero if anything failed; each failure's own output is
# printed below the summary so the reason is in front of you.
set -u
cd "$(dirname "$0")/.."

MODE=${1:-full}
WORK=$(mktemp -d /tmp/soupos-check.XXXXXX)
cleanup() { [ "${KEEP:-0}" = "1" ] && echo "logs kept in $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT
T0=$(date +%s)
now() { echo $(( $(date +%s) - T0 )); }

# ── 0. the default build, here, which everything below reads ──────────────
make clean >/dev/null 2>&1
if ! make >"$WORK/build-default.log" 2>&1; then
    echo "  FAIL  default build"; grep -E "error|undefined" "$WORK/build-default.log" | head -5; exit 1
fi
echo "  ok    default build ($(now)s)"

# Every test boots a COPY of disk.img; one that booted the original changed it
# for every later run (console-test did, until v0.41.0). Fingerprint it now
# and compare at the end.
DISK_SUM=$(sha256sum disk.img | cut -d' ' -f1)

# ── 1. everything else, at once ──────────────────────────────────────────────
# A job writes its exit status to $WORK/<name>.status when done.
#
# At most CHECK_JOBS at a time (2026-10-09; default twice the cores). All
# ~200 at once is ~200 QEMUs of 128 MB each, which is what made the laptop
# unusable and would overrun the 8 GB of the test container on atlas
# (scripts/remote-check.sh). A job's time in its .status is its own, from
# when it started, not from when it was queued.
JOBS=${CHECK_JOBS:-$(( $(nproc) * 2 ))}
PIDS=()
run() {                                   # run <name> <command...>
    local name=$1; shift
    while :; do
        local live=() p
        for p in "${PIDS[@]}"; do kill -0 "$p" 2>/dev/null && live+=("$p"); done
        PIDS=("${live[@]}")
        [ "${#PIDS[@]}" -lt "$JOBS" ] && break
        wait -n 2>/dev/null || sleep 0.2
    done
    ( s=$(date +%s); "$@" >"$WORK/$name.log" 2>&1; echo "$? $(( $(date +%s) - s ))" >"$WORK/$name.status" ) &
    PIDS+=($!)
}

# A copy of the tree for anything that rebuilds it. rsync skips objects and
# the .git directory, so a copy is a few seconds.
tree_copy() {                             # tree_copy <dir>
    rsync -a --exclude .git --exclude '*.o' --exclude '*.d' ./ "$1/"
}

build_cfg() {                             # build_cfg <dir> <make args...>
    local dir=$1; shift
    tree_copy "$dir" && ( cd "$dir" && make clean >/dev/null 2>&1 && make "$@" )
}

run "build FB=0"                build_cfg "$WORK/t-fb0"  FB=0
run "build DOOM=0"              build_cfg "$WORK/t-nd"   DOOM=0
run "build CHALLENGE=1 DOOM=0"  build_cfg "$WORK/t-ch"   CHALLENGE=1 DOOM=0
run "build PROFILE=1"           build_cfg "$WORK/t-pr"   PROFILE=1
run "gate"                      ./scripts/smoke-test.sh

if [ "$MODE" = "full" ]; then
    for t in ssh-test pass-test hatch-test console-test fat-longname-test \
             random-test x25519-test sound-test mixer-test music-test \
             framebuffer-test fullness-test lines-test fsck-test ls-test users-test rc-test fsops-test guess-test owner-test cooks-test bowls-test last-test idle-test du-test secrets-test privs-test reserve-test motd-test append-test grep-test headtail-test sort-test redirect-test uniq-test wc-test tee-test cut-test tr-test chain-test status-test quote-test tilde-test bquote-test run-test lname-test seq-test vars-test testcmd-test glob-test stat-test lsl-test find-test control-test cmp-test arith-test while-test args-test subst-test read-test profile-test flip-test label-test tab-test clockout-test clip-test mince-test knead-test layer-test leftovers-test search-test case-test lint-test stderr-test func-test stack-test spread-test env-test bpipe-test longword-test cd-test loopin-test pipeloop-test wordsplit-test litexp-test assign-test prefix-test param-test until-test break-test herestr-test return-test rest-test dish-test arithcmd-test heredoc-test backslash-test menu-test bracket-test stash-test errexit-test varrandom-test shift-test bang-test loopredir-test amp-test ternary-test trim-test paramsub-test bits-test taker-test promptdoc-test quoteglob-test docsubst-test wide-test inspect-test dishv-test elif-test multiline-test group-test seal-test pluck-test atstar-test tastebi-test bihere-test cont-test trap-test blockdoc-test slurpopt-test casevar-test catsubst-test eggtimer-test array-test brace-test nickname-test dot-test jobspec-test histexp-test biredir-test shellvars-test takeopt-test casefall-test keys-test shelf-test plain-test restn-test tabvar-test select-test colon-test peelstalk-test brine-test brand-test encore-test substnl-test brew-test potluck-test portion-test dbracket-test fnkw-test temper-test rail-test runner-test tumble-test ere-test rematch-test stock-test spot-test carve-test reckon-test bigdir-test ansic-test slicec-test skimdregs-test cullui-test bglob-test expiry-test swapclass-test rackfb-test siftfiles-test weighfiles-test cfor-test taked-test nlopts-test cmpls-test pxmouse-test countertop-test ctwin-test ctterm-test ctbar-test ctfiles-test frost-test prod-test prodwin-test ctwheel-test sear-test swirl-test ctjot-test; do
        run "$t" "./scripts/$t.sh"
    done
    # challenge-test rebuilds as CHALLENGE=1, so it gets a tree of its own too.
    run "challenge-test" bash -c "$(declare -f tree_copy); tree_copy '$WORK/t-chal' && cd '$WORK/t-chal' && ./scripts/challenge-test.sh"
    # doom-fb-test rebuilds two ISOs, so it gets a tree of its own.
    run "doom-fb-test" bash -c "$(declare -f tree_copy); tree_copy '$WORK/t-doom' && cd '$WORK/t-doom' && ./scripts/doom-fb-test.sh"
fi

wait

# ── 2. the summary ───────────────────────────────────────────────────────────
fail=0
: >"$WORK/summary"
for st in "$WORK"/*.status; do
    name=$(basename "$st" .status)
    read -r code secs <"$st"
    extra=""
    [ "$name" = "gate" ] && extra=" $(grep -cE '^  ok' "$WORK/gate.log") checks"
    if [ "$code" = "0" ]; then
        printf '  ok    %-28s %4ss%s\n' "$name" "$secs" "$extra" >>"$WORK/summary"
    else
        printf '  FAIL  %-28s %4ss%s\n' "$name" "$secs" "$extra" >>"$WORK/summary"
        fail=1
    fi
done
if [ "$(sha256sum disk.img | cut -d' ' -f1)" = "$DISK_SUM" ]; then
    printf '  ok    %-28s %4ss\n' "disk.img untouched" 0 >>"$WORK/summary"
else
    printf '  FAIL  %-28s %4ss\n' "disk.img changed by a test" 0 >>"$WORK/summary"
    echo "1 0" >"$WORK/disk.img changed by a test.status"; : >"$WORK/disk.img changed by a test.log"
    fail=1
fi
sort -k3 -n "$WORK/summary"
echo "  total ${MODE} check: $(now)s"

if [ $fail = 1 ]; then
    for st in "$WORK"/*.status; do
        read -r code _ <"$st"
        [ "$code" = "0" ] && continue
        name=$(basename "$st" .status)
        echo; echo "── $name ──"
        grep -E "FAIL|error|undefined|Traceback" "$WORK/$name.log" | head -12
    done
    echo; echo "CHECK FAILED"
else
    echo "CHECK PASSED"
fi
exit $fail

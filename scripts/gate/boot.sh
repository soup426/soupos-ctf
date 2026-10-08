#!/usr/bin/env bash
# Boot, ring 3, preemption, address spaces, and two processes at once.
source "$(dirname "$0")/lib.sh"
gate_init boot
gate_boot
# spin.elf runs ~3s and yieldbg ~6s, so they overlap on purpose. The two
# hello.elf runs at the end happen with nothing else alive, which the
# address-space check depends on.
gate_drive \
    "cook systest.elf hello world" "WAIT:3" \
    "cook hello.elf" "WAIT:2" \
    "preempttest" "WAIT:8" \
    "vmtest" "WAIT:6" \
    "yieldbg" "cook spin.elf" "WAIT:11" \
    "cook marker.elf A &" "cook marker.elf B &" "WAIT:7" \
    "jobs" "WAIT:1" \
    "cook lazy.elf" "WAIT:3" \
    "cook lazy.elf greedy" "WAIT:8" \
    "cook lazy.elf absurd" "WAIT:5" \
    "cook lazy.elf read" "WAIT:2" \
    "cook crash.elf" "WAIT:2" \
    "cook hello.elf" "WAIT:2" "cook hello.elf" "WAIT:2"

check "boot reaches the scheduler"   '\[boot\] scheduler ok'
check "preemption armed"             '\[boot\] preemption enabled'
check "ring3 write syscall"          'USERELF: ring3 syscall write ok'
check "ring3 args syscall"           'TESTOUT args=hello world'
check "ring3 open/read syscall"      'TESTOUT read=Hello from soupOS'
check "ring3 sbrk syscall"           'TESTOUT heap='
check "ring3 yield reaches sched"    '\[u\] step'
check "background task ran"          '\[bg\] done'
check "preempt_disable works"        'PASS'
check "address spaces isolated"      '\[vmtest\] PASS'
check "two processes both ran to completion" '\[p:A\] step 39'
check "second process ran to completion"    '\[p:B\] step 39'

# Demand paging: sbrk promises 16 MB, four times the page cap, and only the
# pages touched get a frame. The greedy run touches every page, meets the
# cap, and is killed - the honest answer - and the shell goes on. crash.elf
# is the fault that was never going to be a heap page, and still kills.
check "a lazy heap costs only the pages touched" 'LAZY touched 64 of 4096 pages ok'
check "the kernel counted the pages it faulted in" '\[demand\] /lazy\.elf faulted in 64 pages'
# With swap past the volume the greedy run completes: 1024 pages stay, the
# other 3072 go out and every one comes back holding its value. The absurd
# run wants more than the cap and the swap together and is killed when the
# swap is full, and the shell goes on.
check "the greedy run completes through swap"     'LAZY touched 4096 of 4096 pages ok'
check "its pages went out and came back"          '\[swap\] /lazy\.elf: [0-9]+ pages out, [0-9]+ back in'
check "past cap and swap together is refused"     '\[demand\] /lazy\.elf: no page for 0x.*swap 4096 of 4096'
check "and that program was killed for it"        '\[user\] fault exc=14 .* -> program killed'
check "swap was found past the volume at boot"    '\[swap\] 4096 pages past the volume'
check "a wild fault still kills" '/crash\.elf exited with code'
# The kernel's own first touch, inside read(): a ring-0 page fault that has
# to be answered the same way, not treated as a panic.
check "a syscall can fault in an untouched page" 'LAZY kernel wrote [0-9]+ bytes into an untouched page ok'

# Two ring-3 programs alive at the same time. marker.elf tags every line with
# its own argument, so an A line between two B lines can only mean both were
# live: one process cannot interleave with itself.
if grep -qE '\[p:A\]' "$LOG" && grep -qE '\[p:B\]' "$LOG"; then
    interleaved=$(between "$(line_of '\[p:A\]')" "$(line_of '\[p:A\]' last)" | grep -cE '\[p:B\]' || true)
    if [ "$interleaved" -gt 0 ]; then
        echo "  ok    two ring-3 processes interleaved ($interleaved B steps inside A's run)"
    else
        echo "  FAIL  no B steps between A's first and last: processes did not overlap"; fail=1
    fi
else
    echo "  FAIL  marker.elf tags missing: concurrent processes never ran"; fail=1
fi

# The per-task ring-0 stack, asserted as a number rather than inferred from
# behaviour. Each process logs the esp0 it entered ring 3 on; the two markers
# are alive at the same time, so theirs MUST differ.
esp_for_tag() {
    local pid
    pid=$(grep -oE "\[proc [0-9]+\] spawned [^ ]+ $1" "$LOG" | grep -oE '[0-9]+' | head -1)
    [ -n "$pid" ] || return 1
    grep -oE "\[proc $pid\] ring3 esp0=0x[0-9a-fA-F]+" "$LOG" | grep -oE '0x[0-9a-fA-F]+' | head -1
}
espA=$(esp_for_tag A || true); espB=$(esp_for_tag B || true)
if [ -n "$espA" ] && [ -n "$espB" ] && [ "$espA" != "$espB" ]; then
    echo "  ok    concurrent processes have their own ring-0 stacks ($espA vs $espB)"
else
    echo "  FAIL  per-task esp0 not proven (A=${espA:-none} B=${espB:-none})"; fail=1
fi

# Address spaces are actually reclaimed: two runs of the SAME program with
# nothing else alive must report the same free page count on exit.
mapfile -t freepages < <(grep -iE '\[proc [0-9]+\] /hello\.elf exited .* pages free' "$LOG" \
                         | grep -oE '[0-9]+ pages free' | grep -oE '^[0-9]+')
n=${#freepages[@]}
if [ "$n" -ge 2 ]; then
    a=${freepages[$((n-2))]}; b=${freepages[$((n-1))]}
    if [ "$a" = "$b" ]; then echo "  ok    no address-space leak (two hello.elf runs both ended at $b free pages)"
    else echo "  FAIL  address space leaked across runs of one program: $a free pages then $b"; fail=1; fi
else
    echo "  FAIL  need two hello.elf exits to compare page counts, found $n"; fail=1
fi

# The point of preemption: the [bg] sleeper must keep ticking while a ring-3
# program is running, i.e. [bg] markers appear between the first and last [u].
if grep -qE '\[bg\]' "$LOG" && grep -qE '\[u\]' "$LOG"; then
    inner=$(between "$(line_of '\[u\] step')" "$(line_of '\[u\] step' last)" | grep -cE '\[bg\]')
    if [ "$inner" -gt 0 ]; then echo "  ok    scheduler interleaved ($inner [bg] ticks during ring-3 run)"
    else echo "  FAIL  no [bg] ticks between the first and last [u] marker"; fail=1; fi
fi
gate_finish

#!/usr/bin/env bash
# Boot, ring 3, preemption, address spaces, and two processes at once.
source "$(dirname "$0")/lib.sh"
gate_init boot
gate_boot
# whisk.elf runs ~3s and yieldbg ~6s, so they overlap on purpose. The two
# greet.elf runs at the end happen with nothing else alive, which the
# address-space check depends on.
gate_drive \
    "cook mise.elf hello world" "WAIT:3" \
    "cook greet.elf" "WAIT:2" \
    "preempttest" "WAIT:8" \
    "vmtest" "WAIT:6" \
    "yieldbg" "cook whisk.elf" "WAIT:11" \
    "cook ticket.elf A &" "cook ticket.elf B &" "WAIT:7" \
    "jobs" "WAIT:1" \
    "cook proof.elf" "WAIT:3" \
    "cook proof.elf greedy" "WAIT:8" \
    "cook proof.elf absurd" "WAIT:5" \
    "cook proof.elf read" "WAIT:2" \
    "cook drop.elf" "WAIT:2" \
    "cook stockpot.elf" "WAIT:3" \
    "cook fridge.elf" "WAIT:3" \
    "cook peek.elf /" "WAIT:2" \
    "cook stockpot.elf forever" "WAIT:3" \
    "cook greet.elf" "WAIT:2" "cook greet.elf" "WAIT:2" \
    "cook proof.elf hold &" "WAIT:2" "ps" "UNTIL:LAZY held"

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
# cap, and is killed - the honest answer - and the shell goes on. drop.elf
# is the fault that was never going to be a heap page, and still kills.
check "a lazy heap costs only the pages touched" 'LAZY touched 64 of 4096 pages ok'
check "the kernel counted the pages it faulted in" '\[demand\] /proof\.elf faulted in 64 pages'
# With swap past the volume the greedy run completes: 1024 pages stay, the
# other 3072 go out and every one comes back holding its value. The absurd
# run wants more than the cap and the swap together and is killed when the
# swap is full, and the shell goes on.
check "the greedy run completes through swap"     'LAZY touched 4096 of 4096 pages ok'
check "its pages went out and came back"          '\[swap\] /proof\.elf: [0-9]+ pages out, [0-9]+ back in'
check "past cap and swap together is refused"     '\[demand\] /proof\.elf: no page for 0x.*swap 4096 of 4096'
check "and that program was killed for it"        '\[user\] fault exc=14 .* -> program killed'
check "swap was found past the volume at boot"    '\[swap\] 4096 pages past the volume'
check "a wild fault still kills" '/drop\.elf exited with code'
# The stack grows on demand to 1 MB, far past the 16 KB it used to be, and
# its guard page turns running off the end into a named kill.
check "a deep stack grows on demand"        'DEEP reached 600 levels ok'
check "the kernel counted the stack growth" '\[stack\] /stockpot\.elf grew its stack by [0-9]+ pages'
check "running off the stack hits the guard" '\[user\] /stockpot\.elf: stack overflow at 0x'

# The victim policy. fridge.elf keeps a 400-page working set hot while
# sweeping 400 new pages a round. FIFO evicts the hot set every round and
# fetches back 1,200 pages; approximate LRU fetches back 400 (one round's
# worth, at the start). The bound sits between them.
check "a working set survives a sweep" 'HOTCOLD done ok'
# A ring-3 program can list a directory (SYS_READDIR). ls-test.sh compares
# the whole listing with mtools; here, that it ran and saw a known file.
check "a program lists a directory" 'LS f 47 HELLO.TXT'
check "and reaches the end of it" 'LS end [0-9]+ entries'
hc_in=$(grep -oE '\[swap\] /fridge\.elf: [0-9]+ pages out, [0-9]+ back in' "$LOG" | grep -oE '[0-9]+ back in' | grep -oE '^[0-9]+')
if [ -n "$hc_in" ] && [ "$hc_in" -lt 600 ]; then
    echo "  ok    the victim policy keeps a working set ($hc_in pages back in; FIFO took 1200)"
else
    echo "  FAIL  the working set was evicted (${hc_in:-no} pages back in; FIFO's 1200 is the bar)"; fail=1
fi
# The kernel's own first touch, inside read(): a ring-0 page fault that has
# to be answered the same way, not treated as a panic.
check "a syscall can fault in an untouched page" 'LAZY kernel wrote [0-9]+ bytes into an untouched page ok'

# Two ring-3 programs alive at the same time. ticket.elf tags every line with
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
    echo "  FAIL  ticket.elf tags missing: concurrent processes never ran"; fail=1
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
mapfile -t freepages < <(grep -iE '\[proc [0-9]+\] /greet\.elf exited .* pages free' "$LOG" \
                         | grep -oE '[0-9]+ pages free' | grep -oE '^[0-9]+')
n=${#freepages[@]}
if [ "$n" -ge 2 ]; then
    a=${freepages[$((n-2))]}; b=${freepages[$((n-1))]}
    if [ "$a" = "$b" ]; then echo "  ok    no address-space leak (two greet.elf runs both ended at $b free pages)"
    else echo "  FAIL  address space leaked across runs of one program: $a free pages then $b"; fail=1; fi
else
    echo "  FAIL  need two greet.elf exits to compare page counts, found $n"; fail=1
fi

# Memory per process in ps (v0.45.0). proof.elf hold touches 1500 heap pages
# and waits: it holds the cap, 1024, resident, and the rest of its pages
# (1500 heap plus its image and stack, less 1024) on disk.
psline=$(grep -E '^ +[0-9]+ +[rRB] +[0-9]+% +[0-9]+ +ring3 +[0-9]+ +[0-9]+ +[a-z]+ +proof\.elf' "$LOG" | tail -1)
res=$(echo "$psline" | awk '{print $6}'); swp=$(echo "$psline" | awk '{print $7}')
if [ "$res" = "1024" ] && [ -n "$swp" ] && [ "$swp" -ge 476 ] && [ "$swp" -le 500 ]; then
    echo "  ok    ps shows a program's memory (1024 resident, $swp swapped)"
    [ "$(echo "$psline" | awk '{print $8}')" = "headchef" ] && echo "  ok    ps shows the program's owner (headchef)" \
        || { echo "  FAIL  ps owner column: '$psline'"; fail=1; }
else
    echo "  FAIL  ps memory figures for proof.elf hold: '${psline:-none}'"; fail=1
fi
# The hold takes 6.5-7 s from its start (4 s asleep, then 481 pages back from
# swap), and the gate used to give it 7.5 s: it failed about one run in three
# on an idle host (2026-10-08). The drive now waits for its line instead.
check "the held pages all came back right" 'LAZY held 1500 pages ok'

# The point of preemption: the [bg] sleeper must keep ticking while a ring-3
# program is running, i.e. [bg] markers appear between the first and last [u].
if grep -qE '\[bg\]' "$LOG" && grep -qE '\[u\]' "$LOG"; then
    inner=$(between "$(line_of '\[u\] step')" "$(line_of '\[u\] step' last)" | grep -cE '\[bg\]')
    if [ "$inner" -gt 0 ]; then echo "  ok    scheduler interleaved ($inner [bg] ticks during ring-3 run)"
    else echo "  FAIL  no [bg] ticks between the first and last [u] marker"; fail=1; fi
fi
gate_finish

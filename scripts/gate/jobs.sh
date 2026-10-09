#!/usr/bin/env bash
# Killing, stopping and resuming programs; pipes and redirection; three
# processes allocating at once. Every count below (two IRQ kills, three
# `killed` lines) is produced entirely by this sequence.
source "$(dirname "$0")/lib.sh"
gate_init jobs
gate_boot
gate_drive \
    "cook glutton.elf &" "WAIT:2" "kill %" "WAIT:2" \
    "cook glutton.elf" "WAIT:2" "KEY:ctrl-c" "WAIT:2" \
    "cook mise.elf afterctrlc" "WAIT:3" \
    "cook ticket.elf Z" "WAIT:1" "KEY:ctrl-z" "WAIT:3" \
    "orders" "WAIT:1" "steep" "WAIT:3" \
    "cook ticket.elf K" "WAIT:1" "KEY:ctrl-z" "WAIT:1" "kill %" "WAIT:2" \
    "cook call.elf hello pipes | weigh.elf" "WAIT:3" \
    "cook call.elf pipes are plumbing | raise.elf | weigh.elf" "WAIT:4" \
    "cook call.elf through a file > P.TXT" "WAIT:2" \
    "cook weigh.elf < P.TXT" "WAIT:3" \
    "cook glutton.elf | weigh.elf &" "WAIT:2" "kill %" "WAIT:2" "kill %" "WAIT:2" \
    "cook measure.elf A | measure.elf B | measure.elf C" "WAIT:14"

check "kill reached a non-cooperating program via the IRQ path" 'killed \(irq\)'

# Ctrl-C on a FOREGROUND program is a different path from `kill %`: the shell
# is blocked in proc_wait and reading no keys, so the keyboard IRQ itself has
# to raise the kill. Two IRQ kills must have happened by now.
kills=$(grep -cE 'killed \(irq\)' "$LOG" || true)
if [ "$kills" -ge 2 ]; then echo "  ok    Ctrl-C killed the foreground program ($kills IRQ kills total)"
else echo "  FAIL  Ctrl-C did not kill the foreground program (only $kills IRQ kills)"; fail=1; fi

# The shell has to survive killing a program: a ring-3 program run AFTER the
# first kill must still produce output.
kill_line=$(line_of 'killed \(irq\)')
if [ -n "$kill_line" ] && after "$kill_line" | grep -E 'TESTOUT' >/dev/null; then
    echo "  ok    shell still runs programs after a kill"
else
    echo "  FAIL  no ring-3 output after the kill: the shell did not survive it"; fail=1
fi

# ── Job control ──
# ticket.elf prints a tagged line every 100 ms, so its output IS the evidence
# of whether it is running. Ctrl-Z it, and those lines must stop.
check "Ctrl-Z stopped the foreground job" '\[proc [0-9]+\] /ticket\.elf stopped'
stop_line=$(line_of '/ticket\.elf stopped'); cont_line=$(line_of '/ticket\.elf continued')
if [ -n "$stop_line" ] && [ -n "$cont_line" ]; then
    while_parked=$(between "$stop_line" "$cont_line" | grep -cE '\[p:Z\]' || true)
    if [ "$while_parked" = "0" ]; then
        echo "  ok    a stopped job really is parked (no output for $(( cont_line - stop_line )) log lines)"
    else
        echo "  FAIL  stopped job kept running: $while_parked lines while parked"
        # This one is RARE, so the next occurrence carries its own evidence:
        # whether the escaping line sits before or after the kernel's own
        # "stopped" message, which proc_take_stop logs at the moment it parks.
        echo "        --- log lines $((stop_line - 2)) to $((cont_line + 1)) ---"
        awk -v a="$((stop_line - 2))" -v b="$((cont_line + 1))" 'NR>=a && NR<=b {printf "        %5d  %s\n", NR, $0}' "$LOG"
        fail=1
    fi
    after_n=$(after "$cont_line" | grep -cE '\[p:Z\]' || true)
    if [ "$after_n" -gt 0 ]; then echo "  ok    bg resumed the stopped job ($after_n more steps)"
    else echo "  FAIL  bg did not resume the job: no output after it continued"; fail=1; fi
else
    echo "  FAIL  job control markers missing (stopped=${stop_line:-none} continued=${cont_line:-none})"; fail=1
fi
if [ -n "${stop_line:-}" ] && after "$stop_line" | grep -E '\[proc [0-9]+\] spawned /ticket\.elf K' >/dev/null; then
    echo "  ok    shell took commands again after Ctrl-Z"
else echo "  FAIL  shell did not come back after Ctrl-Z"; fail=1; fi
# A stopped job is parked on a wait queue and would never reach a safe point
# on its own, so kill has to wake it to let it die.
last_stop=$(line_of '/ticket\.elf stopped' last)
if [ -n "$last_stop" ] && after "$last_stop" | grep -E '\[proc [0-9]+\] killed' >/dev/null; then
    echo "  ok    a stopped job can still be killed"
else echo "  FAIL  killing a stopped job did not end it"; fail=1; fi

# ── Pipes and redirection ──
check "two-stage pipeline carries the data"  'WCOUT lines=1 words=2 bytes=12'
check "three-stage pipeline works"           'WCOUT lines=1 words=3 bytes=19'
check "the middle stage ran and exited"      '/raise\.elf exited with code 0'
check "> redirection reached the filesystem" 'WCOUT lines=1 words=3 bytes=15'
# A reader parked on a pipe nobody will write to must still be killable.
if grep -qE '/weigh\.elf' "$LOG" && [ "$(grep -cE '\[proc [0-9]+\] killed' "$LOG")" -ge 3 ]; then
    echo "  ok    a process parked on an empty pipe can be killed"
else echo "  FAIL  a process blocked on a pipe survived kill"; fail=1; fi
# `kill %` on a backgrounded pipeline: the flag lands on weigh.elf, blocked in a
# pipe read; it must be woken to die, or it stays the newest process and the
# second kill hits it again while glutton.elf runs on at 90%.
check "killing a reader blocked on a pipe works" '/weigh.elf exited with code -137'
check "the second kill then reaches the hog" 'killing process [0-9]+ \(/glutton.elf\)'

# ── Concurrent allocation ──
# Three processes started back-to-back by one pipeline, so their ELF loads
# and sbrk calls are inside the page allocator at the same time. Each fills
# its own pages with its own tag and checks they still say what it wrote.
for tag in A B C; do check "concurrent process $tag kept its own pages" "MEMTEST OK tag=$tag"; done
gate_finish

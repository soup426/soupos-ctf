#!/usr/bin/env bash
# soupOS headless smoke test — "the oracle".
#
# Boots the ISO with no display, drives the shell through the QEMU monitor
# (sendkey), and greps the COM1 serial log for the markers each subsystem
# emits. Exits non-zero on the first missing marker, so it works as a gate.
#
#   ./scripts/smoke-test.sh            # build if needed, then test
#   KEEP=1 ./scripts/smoke-test.sh     # keep the serial log for inspection
#
# Requires: qemu-system-i386, python3, and a built soupOS.iso + disk.img.
set -uo pipefail
cd "$(dirname "$0")/.."

LOG=$(mktemp /tmp/soupos-smoke.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-smoke.XXXXXX.sock)
QEMU_PID=""

cleanup() {
    [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null
    rm -f "$MON"
    if [ "${KEEP:-0}" = "1" ]; then echo "serial log kept at $LOG"; else rm -f "$LOG"; fi
}
trap cleanup EXIT

[ -f soupOS.iso ] || { echo "soupOS.iso missing — run 'make' first"; exit 1; }
[ -f disk.img ]   || { echo "disk.img missing — run 'make disk' first"; exit 1; }

echo "booting soupOS headless..."
qemu-system-i386 \
    -drive file=disk.img,format=raw,if=ide,index=0 \
    -cdrom soupOS.iso -boot d \
    -m 128M -no-reboot -no-shutdown \
    -netdev user,id=n0 -device rtl8139,netdev=n0 \
    -display none \
    -serial "file:$LOG" \
    -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
QEMU_PID=$!
sleep 4

# Log in as the default headchef, then exercise preemption and ring 3.
# spin.elf runs ~3s and yieldbg ~6s, so they overlap on purpose.
python3 scripts/qemu_keys.py "$MON" \
    "headchef" "rosemary" "WAIT:1" \
    "cook systest.elf hello world" "WAIT:3" \
    "cook hello.elf" "WAIT:2" \
    "preempttest" "WAIT:8" \
    "fatstress" "WAIT:30" \
    "vgastress" "WAIT:25" \
    "vmtest" "WAIT:6" \
    "yieldbg" "cook spin.elf" "WAIT:11" \
    "cook marker.elf A &" "cook marker.elf B &" "WAIT:7" \
    "jobs" "WAIT:1" \
    "cook hog.elf &" "WAIT:2" "kill %" "WAIT:2" \
    "cook hog.elf" "WAIT:2" "KEY:ctrl-c" "WAIT:2" \
    "cook systest.elf afterctrlc" "WAIT:3" \
    "cook systest.elf post" "WAIT:3" \
    "cook hello.elf" "WAIT:2" "cook hello.elf" "WAIT:2" \
    "cook marker.elf Z" "WAIT:1" "KEY:ctrl-z" "WAIT:3" \
    "jobs" "WAIT:1" "bg" "WAIT:3" \
    "cook marker.elf K" "WAIT:1" "KEY:ctrl-z" "WAIT:1" "kill %" "WAIT:2" \
    "cook echo.elf hello pipes | wc.elf" "WAIT:3" \
    "cook echo.elf pipes are plumbing | upper.elf | wc.elf" "WAIT:4" \
    "cook echo.elf through a file > P.TXT" "WAIT:2" \
    "cook wc.elf < P.TXT" "WAIT:3" \
    "cook hog.elf | wc.elf &" "WAIT:2" "kill %" "WAIT:2" "kill %" "WAIT:2" \
    "strain CLIP.TXT" "WAIT:1" \
    "TYPE:soup is good" "KEY:ctrl-u" "clip" "WAIT:2" \
    "jot CLIP.TXT" "WAIT:2" "TYPE:abc def" \
    "KEY:home" "KEY:ctrl-b" "KEY:end" "KEY:ctrl-c" "KEY:ctrl-v" \
    "KEY:ctrl-s" "WAIT:1" "KEY:esc" "WAIT:1" \
    "cook wc.elf < CLIP.TXT" "WAIT:3" \
    "TYPE:clip" "KEY:ctrl-u" "KEY:ctrl-v" "KEY:ret" "WAIT:2" \
    "cook memtest.elf A | memtest.elf B | memtest.elf C" "WAIT:14" \
    "ping 10.0.2.2 3" "WAIT:8" >/dev/null || {
        echo "FAIL: could not drive the QEMU monitor"; exit 1; }

fail=0
check() {   # check <description> <grep-pattern>
    if grep -qE "$2" "$LOG"; then
        echo "  ok    $1"
    else
        echo "  FAIL  $1  (no match for /$2/)"
        fail=1
    fi
}

echo "checking serial markers:"
check "boot reaches the scheduler"   '\[boot\] scheduler ok'
check "preemption armed"             '\[boot\] preemption enabled'
check "ring3 write syscall"          'USERELF: ring3 syscall write ok'
check "ring3 args syscall"           'TESTOUT args=hello world'
check "ring3 open/read syscall"      'TESTOUT read=Hello from soupOS'
check "ring3 sbrk syscall"           'TESTOUT heap='
check "ring3 yield reaches sched"    '\[u\] step'
check "background task ran"          '\[bg\] done'
check "preempt_disable works"       'PASS'
check "concurrent FAT is safe"      '\[fatstress\] PASS'
check "address spaces isolated"     '\[vmtest\] PASS'

check "two processes both ran to completion" '\[p:A\] step 39'
check "second process ran to completion"    '\[p:B\] step 39'
check "kill reached a non-cooperating program via the IRQ path" 'killed \(irq\)'

# Two ring-3 programs alive at the same time. marker.elf tags every line with
# its own argument, so an A line between two B lines can only mean both were
# live: one process cannot interleave with itself.
if grep -qE '\[p:A\]' "$LOG" && grep -qE '\[p:B\]' "$LOG"; then
    firstA=$(grep -nE '\[p:A\]' "$LOG" | head -1 | cut -d: -f1)
    lastA=$( grep -nE '\[p:A\]' "$LOG" | tail -1 | cut -d: -f1)
    interleaved=$(awk -v a="$firstA" -v b="$lastA" 'NR>a && NR<b' "$LOG" \
                  | grep -cE '\[p:B\]' || true)
    if [ "$interleaved" -gt 0 ]; then
        echo "  ok    two ring-3 processes interleaved ($interleaved B steps inside A's run)"
    else
        echo "  FAIL  no B steps between A's first and last: processes did not overlap"
        fail=1
    fi
else
    echo "  FAIL  marker.elf tags missing: concurrent processes never ran"
    fail=1
fi

# The per-task ring-0 stack, asserted as a number rather than inferred from
# behaviour. Each process logs the esp0 it entered ring 3 on; the two markers
# are alive at the same time, so theirs MUST differ. If they matched, one
# program's trap frame would be landing on the other's.
esp_for_tag() {   # esp_for_tag <marker arg>
    local pid
    pid=$(grep -oE "\[proc [0-9]+\] spawned [^ ]+ $1" "$LOG" \
          | grep -oE '[0-9]+' | head -1)
    [ -n "$pid" ] || return 1
    grep -oE "\[proc $pid\] ring3 esp0=0x[0-9a-fA-F]+" "$LOG" \
        | grep -oE '0x[0-9a-fA-F]+' | head -1
}
espA=$(esp_for_tag A || true)
espB=$(esp_for_tag B || true)
if [ -n "$espA" ] && [ -n "$espB" ] && [ "$espA" != "$espB" ]; then
    echo "  ok    concurrent processes have their own ring-0 stacks ($espA vs $espB)"
else
    echo "  FAIL  per-task esp0 not proven (A=${espA:-none} B=${espB:-none})"
    fail=1
fi

# Ctrl-C on a FOREGROUND program. Different path from `kill %`: the shell is
# blocked in proc_wait and reading no keys, so the kill has to be raised by the
# keyboard IRQ itself. Two kills must have happened by now, and the second one
# is this.
kills=$(grep -cE 'killed \(irq\)' "$LOG" || true)
if [ "$kills" -ge 2 ]; then
    echo "  ok    Ctrl-C killed the foreground program ($kills IRQ kills total)"
else
    echo "  FAIL  Ctrl-C did not kill the foreground program (only $kills IRQ kills)"
    fail=1
fi

# The shell has to survive killing a program: a ring-3 program run AFTER the
# kill must still produce output.
# The FIRST irq kill is the dedicated one from `cook hog.elf &` + `kill %`,
# which the drive script follows immediately with a systest run. Anchoring to
# the last one instead would point past it, because the pipe tests kill things
# at the end of the script.
kill_line=$(grep -nE 'killed \(irq\)' "$LOG" | head -1 | cut -d: -f1)
if [ -n "$kill_line" ] && awk -v k="$kill_line" 'NR>k' "$LOG" | grep -qE 'TESTOUT'; then
    echo "  ok    shell still runs programs after a kill"
else
    echo "  FAIL  no ring-3 output after the kill: the shell did not survive it"
    fail=1
fi

# Address spaces are actually reclaimed. Every process logs the free page count
# once its own space is torn down, so two runs of the SAME program with nothing
# else alive must report the same number. It has to be the same program and no
# overlap: a process that exits while another is still running legitimately
# sees fewer free pages, because the other one still holds its space.
mapfile -t freepages < <(grep -iE '\[proc [0-9]+\] /hello\.elf exited .* pages free' "$LOG" \
                         | grep -oE '[0-9]+ pages free' | grep -oE '^[0-9]+')
n=${#freepages[@]}
if [ "$n" -ge 2 ]; then
    a=${freepages[$((n-2))]}; b=${freepages[$((n-1))]}
    if [ "$a" = "$b" ]; then
        echo "  ok    no address-space leak (two hello.elf runs both ended at $b free pages)"
    else
        echo "  FAIL  address space leaked across runs of one program: $a free pages then $b"
        fail=1
    fi
else
    echo "  FAIL  need two hello.elf exits to compare page counts, found $n"
    fail=1
fi

# ── Job control ───────────────────────────────────────────────────────────
# marker.elf prints a tagged line every 100 ms, so its output IS the evidence
# of whether it is running. Ctrl-Z it, and those lines must stop.
check "Ctrl-Z stopped the foreground job" '\[proc [0-9]+\] /marker\.elf stopped'

stop_line=$(grep -nE '/marker\.elf stopped' "$LOG" | head -1 | cut -d: -f1)
cont_line=$(grep -nE '/marker\.elf continued' "$LOG" | head -1 | cut -d: -f1)
if [ -n "$stop_line" ] && [ -n "$cont_line" ]; then
    while_parked=$(awk -v a="$stop_line" -v b="$cont_line" 'NR>a && NR<b' "$LOG" \
                   | grep -cE '\[p:Z\]' || true)
    if [ "$while_parked" = "0" ]; then
        echo "  ok    a stopped job really is parked (no output for $(( cont_line - stop_line )) log lines)"
    else
        echo "  FAIL  stopped job kept running: $while_parked lines while parked"
        fail=1
    fi
    after=$(awk -v b="$cont_line" 'NR>b' "$LOG" | grep -cE '\[p:Z\]' || true)
    if [ "$after" -gt 0 ]; then
        echo "  ok    bg resumed the stopped job ($after more steps)"
    else
        echo "  FAIL  bg did not resume the job: no output after it continued"
        fail=1
    fi
else
    echo "  FAIL  job control markers missing (stopped=${stop_line:-none} continued=${cont_line:-none})"
    fail=1
fi

# The point of Ctrl-Z: the shell comes back. Something it ran afterwards must
# have produced output.
if [ -n "${stop_line:-}" ] && awk -v a="$stop_line" 'NR>a' "$LOG" | grep -qE '\[proc [0-9]+\] spawned /marker\.elf K'; then
    echo "  ok    shell took commands again after Ctrl-Z"
else
    echo "  FAIL  shell did not come back after Ctrl-Z"
    fail=1
fi

# A stopped job is parked on a wait queue and would never reach a safe point on
# its own, so kill has to wake it to let it die.
last_stop=$(grep -nE '/marker\.elf stopped' "$LOG" | tail -1 | cut -d: -f1)
if [ -n "$last_stop" ] && awk -v a="$last_stop" 'NR>a' "$LOG" | grep -qE '\[proc [0-9]+\] killed'; then
    echo "  ok    a stopped job can still be killed"
else
    echo "  FAIL  killing a stopped job did not end it"
    fail=1
fi

# ── Pipes and redirection ─────────────────────────────────────────────────
# wc.elf echoes its counts to serial, so a pipeline's RESULT is readable
# headless rather than inferred from the stages having run.
check "two-stage pipeline carries the data"  'WCOUT lines=1 words=2 bytes=12'
check "three-stage pipeline works"           'WCOUT lines=1 words=3 bytes=19'
check "the middle stage ran and exited"      '/upper\.elf exited with code 0'
check "> redirection reached the filesystem" 'WCOUT lines=1 words=3 bytes=15'

# A reader parked on a pipe nobody will write to must still be killable: hog.elf
# never writes and never exits, so wc.elf blocks forever on the read. Without
# the kill check inside the pipe wait, this is an unkillable process.
if grep -qE '/wc\.elf' "$LOG" && [ "$(grep -cE '\[proc [0-9]+\] killed' "$LOG")" -ge 3 ]; then
    echo "  ok    a process parked on an empty pipe can be killed"
else
    echo "  FAIL  a process blocked on a pipe survived kill"
    fail=1
fi

# ── Clipboard ─────────────────────────────────────────────────────────────
# Ctrl+U kills the line into the clipboard, and `clip` mirrors the contents to
# the log, so the copy is checked by its actual text rather than its length.
check "Ctrl+U copies the killed text" '\[clip\] 12 bytes: soup is good'

# jot: mark the whole line with Ctrl+B, copy it, paste it at the end, save.
# "abc def" copied and pasted gives "abc defabc def": 14 bytes, 3 words, and no
# newline. The file is deleted first (strain) so a second run sees the same
# starting state as the first.
check "jot copies and pastes a marked span" 'WCOUT lines=0 words=3 bytes=14'

# And the paste side at the prompt: the word "clip" is killed into the
# clipboard and pasted straight back, so the command only runs at all if
# Ctrl+V put it there.
check "Ctrl+V pastes at the prompt" '\[clip\] 4 bytes: clip'

# ── Concurrent allocation ─────────────────────────────────────────────────
# Three processes started back-to-back by one pipeline, so their ELF loads and
# their sbrk calls are inside the physical page allocator at the same time.
# Each fills its own pages with its own tag, yields, and checks they still say
# what it wrote: if the allocator ever hands one frame to two processes, the
# tags collide and whoever checks second sees the other's letter.
for tag in A B C; do
    check "concurrent process $tag kept its own pages" "MEMTEST OK tag=$tag"
done

# ── Networking ────────────────────────────────────────────────────────────
# The gate's QEMU has a user-mode network with an RTL8139 on it, so the whole
# path is exercised for real: PCI probe, DMA ring, IRQ 11 through the slave
# PIC, ARP for the gateway, then ICMP. A reply means every layer worked.
check "the NIC was found and configured" '\[net\] rtl8139 at io'
check "ARP resolved and all pings replied" '\[net\] ping done 3/3'

# vgastress prints whole lines of a single repeated character from two tasks.
# Any line containing both means vga_puts was interrupted mid-string.
mixed=$(sed -n '/\[vgastress\] begin/,/\[vgastress\] end/p' "$LOG" \
        | grep -c 'A.*B\|B.*A' || true)
if [ "$mixed" = "0" ]; then
    echo "  ok    vga output not interleaved"
else
    echo "  FAIL  vga output interleaved on $mixed lines"
    fail=1
fi

# The point of preemption: the [bg] sleeper must keep ticking while a
# ring-3 program is running, i.e. [bg] markers appear on both sides of [u].
if grep -qE '\[bg\]' "$LOG" && grep -qE '\[u\]' "$LOG"; then
    first_u=$(grep -nE '\[u\] step'  "$LOG" | head -1 | cut -d: -f1)
    last_u=$( grep -nE '\[u\] step'  "$LOG" | tail -1 | cut -d: -f1)
    inner=$(awk -v a="$first_u" -v b="$last_u" 'NR>a && NR<b' "$LOG" | grep -cE '\[bg\]')
    if [ "$inner" -gt 0 ]; then
        echo "  ok    scheduler interleaved ($inner [bg] ticks during ring-3 run)"
    else
        echo "  FAIL  no [bg] ticks between the first and last [u] marker"
        fail=1
    fi
fi

# Nothing should have panicked.
if grep -qE 'KERNEL PANIC' "$LOG"; then
    echo "  FAIL  kernel panicked:"
    grep -A6 'KERNEL PANIC' "$LOG" | sed 's/^/        /'
    fail=1
else
    echo "  ok    no kernel panic"
fi

echo
if [ "$fail" = 0 ]; then echo "SMOKE TEST PASSED"; else echo "SMOKE TEST FAILED"; fi
exit $fail

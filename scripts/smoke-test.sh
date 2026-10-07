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

LISTENER_PIDS=""
LOG=$(mktemp /tmp/soupos-smoke.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-smoke.XXXXXX.sock)
WAV=$(mktemp -u /tmp/soupos-smoke.XXXXXX.wav)
QEMU_PID=""

cleanup() {
    [ -n "$LISTENER_PIDS" ] && kill $LISTENER_PIDS 2>/dev/null
    [ -n "$QEMU_PID" ] && kill "$QEMU_PID" 2>/dev/null
    rm -f "$MON" "$TESTDISK" "$WAV"
    if [ "${KEEP:-0}" = "1" ]; then echo "serial log kept at $LOG"; else rm -f "$LOG"; fi
}
trap cleanup EXIT

[ -f soupOS.iso ] || { echo "soupOS.iso missing — run 'make' first"; exit 1; }
[ -f disk.img ]   || { echo "disk.img missing — run 'make disk' first"; exit 1; }

# Something for the TCP checks to connect to. QEMU's user networking maps the
# guest's 10.0.2.2 to the host's loopback, so a local listener is reachable
# from inside soupOS without any external network being involved.
#
# One port per check, deliberately. Three connections in a row to the SAME
# SLIRP port leave the third unanswered - QEMU-side state that outlives our
# close, reproducible by hand and not worth working around in the kernel.
#
# The accept timeout is a budget for the WHOLE drive sequence, not for one
# connection: when it expires the listener exits and everything afterwards gets
# a refused connection. Adding checks ahead of the TCP ones eats into it, which
# is exactly how the mouse work broke these three. Keep it comfortably above the
# full run time.
for port in 19000 19001 19002; do
    python3 -c "
import socket, sys
port = int(sys.argv[1])
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port)); s.listen(8); s.settimeout(900)
try:
    while True:
        c, _ = s.accept()
        c.settimeout(5)
        try:
            d = c.recv(1024)
            if d.startswith(b'GET'):          # serve the takeout check
                c.sendall(b'HTTP/1.0 200 OK\\r\\n\\r\\nsoup is served\\n')
            elif d:                           # echo, for the data-path check
                c.sendall(d.upper())
        except Exception:
            pass
        c.close()
except Exception:
    pass
" "$port" >/dev/null 2>&1 &
    LISTENER_PIDS="$LISTENER_PIDS $!"
done

echo "booting soupOS headless..."
# A throwaway copy of the disk, for two reasons: an interactive QEMU window
# holding disk.img must not block the tests (the image is write-locked), and
# a hermetic disk means no state leaks between runs - stale files in the
# image have caused a flaky check before.
TESTDISK=$(mktemp /tmp/soupos-testdisk.XXXXXX.img)
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
    "orders" "WAIT:1" "steep" "WAIT:3" \
    "cook marker.elf K" "WAIT:1" "KEY:ctrl-z" "WAIT:1" "kill %" "WAIT:2" \
    "cook echo.elf hello pipes | wc.elf" "WAIT:3" \
    "cook echo.elf pipes are plumbing | upper.elf | wc.elf" "WAIT:4" \
    "cook echo.elf through a file > P.TXT" "WAIT:2" \
    "cook wc.elf < P.TXT" "WAIT:3" \
    "cook hog.elf | wc.elf &" "WAIT:2" "kill %" "WAIT:2" "kill %" "WAIT:2" \
    "sample" "WAIT:6" \
    "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" \
    "MON:mouse_move 40 20" "skewer" "WAIT:2" \
    "strain MOUSE.TXT" "jot MOUSE.TXT" "WAIT:2" "TYPE:abcdefghij" "WAIT:1" \
    "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "MON:mouse_move -120 -120" "WAIT:1" \
    "MON:mouse_button 1" "WAIT:1" "MON:mouse_move 20 0" "WAIT:1" "MON:mouse_button 0" "WAIT:1" \
    "KEY:ctrl-c" "WAIT:1" "KEY:ctrl-s" "WAIT:1" "KEY:esc" "WAIT:2" "scraps" "WAIT:2" \
    "strain CLIP.TXT" "WAIT:1" \
    "TYPE:soup is good" "KEY:ctrl-u" "scraps" "WAIT:2" \
    "jot CLIP.TXT" "WAIT:2" "TYPE:abc def" \
    "KEY:home" "KEY:ctrl-b" "KEY:end" "KEY:ctrl-c" "KEY:ctrl-v" \
    "KEY:ctrl-s" "WAIT:1" "KEY:esc" "WAIT:1" \
    "cook wc.elf < CLIP.TXT" "WAIT:3" \
    "TYPE:scraps" "KEY:ctrl-u" "KEY:ctrl-v" "KEY:ret" "WAIT:2" \
    "cook memtest.elf A | memtest.elf B | memtest.elf C" "WAIT:14" \
    "sip 10.0.2.2 3" "WAIT:8" \
    "sniff example.com" "WAIT:6" \
    "reserve 10.0.2.2 19000" "WAIT:6" \
    "holler 10.0.2.2 19001 souptcp" "WAIT:8" \
    "takeout 10.0.2.2:19002 /soup" "WAIT:9" \
    "soup LONGSTR.SC" "WAIT:6" \
    "soup SCOPE.SC" "WAIT:4" \
    "serve" "WAIT:2" \
    "pour A Long Recipe Name.txt" "WAIT:2" \
    "pour ALONGR~1.TXT" "WAIT:2" \
    "soup RUNAWAY.SC" "WAIT:22" "pwd" "WAIT:2" \
    "soup SPAWN.SC" "WAIT:1" "ps" "WAIT:5" \
    "larder" "WAIT:2" \
    "whistle 440 300" "WAIT:4" \
    "sizzle DSPISTOL" "WAIT:4" \
    "hum D_E1M1" "WAIT:3" "hum" "WAIT:2" >/dev/null || {
        echo "FAIL: could not drive the QEMU monitor"; exit 1; }

fail=0
check() {   # check <description> [-cN] <grep-pattern>
    # -cN asserts the pattern appears at least N times, which is how a repeat
    # of the same output (two opens of one file) gets distinguished from one.
    if [ "${2#-c}" != "$2" ]; then
        local want=${2#-c}
        local got
        got=$(grep -cE "$3" "$LOG")
        if [ "$got" -ge "$want" ]; then
            echo "  ok    $1"
        else
            echo "  FAIL  $1  (saw $got of $want for /$3/)"
            fail=1
        fi
        return
    fi
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
        # Print the boundary, because this one is RARE - twenty-four targeted
        # runs on 2026-10-07 never reproduced it - so the next occurrence has
        # to carry its own evidence or the investigation starts from nothing
        # again. What matters is whether the escaping line sits before or
        # after the kernel's own "stopped" message in time, not just in the
        # file: proc_take_stop logs it at the moment it parks.
        echo "        --- log lines $((stop_line - 2)) to $((cont_line + 1)) ---"
        awk -v a="$((stop_line - 2))" -v b="$((cont_line + 1))" \
            'NR>=a && NR<=b {printf "        %5d  %s\n", NR, $0}' "$LOG"
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
check "Ctrl+V pastes at the prompt" '\[clip\] 6 bytes: scraps'

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

# DHCP is hermetic: SLIRP serves it, so this needs no real network. The lease
# line only appears after DISCOVER/OFFER/REQUEST/ACK have all completed and the
# ACK's options have been read.
check "an address was taken by DHCP" '\[dhcp\] lease 10\.0\.2\.15 mask 255\.255\.255\.0'
check "ARP resolved and all pings replied" '\[net\] ping done 3/3'

# The UDP path end to end, as far as the wire: the resolver's MAC is resolved,
# a datagram is built and handed to the card. The ANSWER is deliberately not
# asserted, because it needs the host to have working DNS, and a gate that
# fails when you are offline is a bad gate.
check "a DNS query was built and sent" '\[net\] dns query id='

# TCP's three-way handshake against a listener on the host, reached through
# QEMU's gateway, so this is hermetic too. "established" is only logged after
# SYN, SYN/ACK and our ACK have all been through the state machine.
check "a TCP connection was established" '\[tcp\] established with 10\.0\.2\.2:19000'

# And the data path: the listener echoes what it is sent, upper-cased, so a
# reply proves our segment arrived, was acknowledged, and theirs was accepted
# in sequence and handed to the reader.
check "TCP carried data and got a reply" '\[tcp\] sent 7 bytes'
check "TCP received the echo"            '\[tcp\] rx 7 bytes'

# The whole stack, end to end: DNS-free connect, an HTTP/1.0 request, a body
# read until the server hangs up, and a FIN exchange in both directions.
check "an HTTP page was fetched over TCP" '\[tcp\] takeout 3[0-9] bytes'
check "the connection closed gracefully"  '\[tcp\] closed cleanly'

# The in-kernel checks: allocator patterns, the handle table at exhaustion, the
# pipe ring across its wrap, a known-answer checksum, a filesystem round trip
# and the hand-written string routines. One line, so the count can grow without
# touching the gate.
check "in-kernel self-test passes" '\[sample\] [0-9]+ tests, 0 failed'

# The mouse, driven through the QEMU monitor rather than by hand. The pointer
# starts at the middle of the console, which is 40,12 on a text boot and 64,24
# on the framebuffer, so it is first driven hard into the top-left corner,
# where it clamps, and only then moved a known amount: 40 raw is ten cells at
# four counts per cell, and a POSITIVE dy moves down the screen. That lands on
# 10,5 whatever size the console is, and still catches a sign error or a
# change of scaling.
check "mouse tracks injected movement" '\[mouse\] at 10,5 '

# Drag-select: press at the start of the line, drag five cells right, release,
# then the ordinary ^C. Three moves into the corner because a PS/2 delta is
# clamped to a byte and the pointer stops at the edge, so this reaches 0,0 from
# anywhere - including the middle of a 128x48 console, which is further from
# the corner than a 80x25 one and needs the extra vertical travel.
# Asserting the content, not the length: a mapping off by one still copies five
# bytes, just the wrong five.
check "mouse drag-select feeds the clipboard" '\[clip\] 5 bytes: abcde'

# soupyc's string values used to be a fixed char[48]. These run LAST in the
# drive sequence on purpose: the runaway check takes twenty seconds and the TCP
# listeners' accept timeout is a budget for everything before them.
# Exact lengths, so a truncation anywhere shows up as the wrong number.
check "soupyc concatenates past 47 chars" 'built 100'
check "soupyc takes a long literal"       'literal 97'
check "soupyc reads 100 bytes of a file"  'file 100'
# And the budget has to fail cleanly rather than taking the kernel with it:
# the shell has to still be there afterwards.
check "runaway strings fail cleanly" 'out of string memory'

# spawn: a script function running as its own task. The five things that have
# to be true, each of which fails differently if the context work is wrong.
check "spawn returns a task id"            '\[parent\] spawned [0-9]+'
# ps catching it mid-flight is the proof it outlives the `soup` command: the
# shell got its prompt back and ran another command while the child ticked.
check "the spawned script is a live task"  'soup:ticker'
# The array pool is SHARED (its layout is fixed for the challenge) with
# per-context ownership of slots. These two prove neither script clears the
# other's: the parent's array is intact after the child allocated one.
check "parent keeps its arrays"            '\[parent\] array still 2 2'
check "child gets arrays of its own"       '\[child\] array 3 8'
check "the spawned script finishes"        '\[child\] done'

# Free space had no way to be seen from inside soupOS before, which is part of
# why a cluster leak could sit in the write path unnoticed. The `sample` suite
# above now also asserts that a write-then-delete gives back exactly what it
# took, which is the shape that leak had.
check "larder reports disk space" 'clusters used \([0-9]+ KB each\)'

# remove() closed a real gap: soupyc could create files but not delete them,
# so a script that filled the disk could not clean up after itself.
check "soupyc can delete a file" 'removed 1'

# A `let` inside a while body used to accumulate a binding per pass and die at
# MAX_VARS, which is 64. 200 iterations, and the exact sum, so a scoping change
# that drops the wrong bindings shows up as a wrong total rather than a crash.
check "a let inside a loop survives 200 passes" 'SCOPE total 39800'

# VFAT long names. The listing must show the name a person typed, and BOTH
# names must open the file: the long one and the mangled 8.3 alias mtools
# generated beside it. The alias is the half that keeps working for everything
# written before long names existed.
check "a long name is listed"            'A Long Recipe Name.txt'
check "a long name opens the file"       'A file whose name does not fit in 8.3'
# Two opens of the same file, so the count proves the 8.3 alias worked too.
check "the 8.3 alias opens it as well"   -c2 'A file whose name does not fit in 8.3'

check "the mixer accepted a tone" '\[mixer\] tone 440 Hz'
check "a Doom sound lump was decoded" '\[doomsnd\] DSPISTOL: 5661 samples at 11025 Hz'
# The score's own numbers, so a mis-parsed header shows up here rather than as
# silence. scripts/music-test.sh checks the notes are the RIGHT notes.
check "a MUS score was parsed" '\[music\] D_E1M1: 17237 bytes of score, 3 channels'
check "the mixer came up" '\[mixer\] 4 voices'
# What the background services actually cost. The renderer repaints 30x a
# second and the mixer wakes 100x; measured idle, the renderer is 0%.
check "a service's cpu share is reported" '\[sample\] busiest service: '

# The fine-grained audio measurements - the tone's frequency and the effect's
# length - moved to scripts/sound-test.sh. They need a capture containing
# nothing else, and this gate now plays a tone, an effect AND music into one
# file: QEMU writes only while the card is active, so three activations land
# contiguously and any span-based measurement sweeps in whatever played next.
# It reported a 4251 ms sound effect before this moved out. What stays here is
# what the serial log can answer on its own.

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

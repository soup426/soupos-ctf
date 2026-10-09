#!/usr/bin/env bash
# Soak the job-control path, looking for two rare faults the gate has shown.
#
# Usage: scripts/jobcontrol-soak.sh [runs]   (default 8)
#
# One gate run failed with "stopped job kept running: 1 lines while parked",
# meaning a marker line appeared in the log between the kernel's "stopped"
# message and "continued". proc_take_stop logs that message at the moment it
# parks, so a line printed on the way to the safe point should land BEFORE it -
# which is why the failure is interesting rather than obviously benign.
#
# Twenty-four runs on 2026-10-07 did not reproduce it. A separate anomaly did
# appear once: the "continued" marker never arrived, so the job did not resume
# (or did not log it). Both are below a few percent, so this script exists to
# be run in bulk when someone has the machine spare.
#
# It drives the gate's own conditions: two background markers already running,
# and an `orders` between the stop and the resume.
cd /home/soup/Projects/soupOS-processes
for run in $(seq 1 "${1:-8}"); do
    TD=$(mktemp /tmp/jf.XXXX.img); cp disk.img "$TD"
    LOG=$(mktemp /tmp/jf.XXXX.log); MON=$(mktemp -u /tmp/jf.XXXX.sock)
    qemu-system-i386 -accel kvm -cpu host -drive "file=$TD,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
    PID=$!
    sleep 5
    # The gate's own conditions: two background markers already running, and
    # an `orders` between the stop and the resume.
    python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
        "cook ticket.elf A &" "cook ticket.elf B &" "WAIT:7" \
        "cook ticket.elf Z" "WAIT:1" "KEY:ctrl-z" "WAIT:3" \
        "orders" "WAIT:1" "steep" "WAIT:3" >/dev/null 2>&1
    kill $PID 2>/dev/null; rm -f "$TD"
    stop=$(grep -nE '/ticket\.elf stopped' "$LOG" | head -1 | cut -d: -f1)
    cont=$(grep -nE '/ticket\.elf continued' "$LOG" | head -1 | cut -d: -f1)
    if [ -n "$stop" ] && [ -n "$cont" ]; then
        n=$(awk -v a="$stop" -v b="$cont" 'NR>a && NR<b' "$LOG" | grep -cE '\[p:Z\]' || true)
        echo "run $run: $n line(s) while parked"
        if [ "$n" != "0" ]; then
            echo "--- lines $((stop-2)) to $((cont+1)) ---"
            awk -v a="$((stop-2))" -v b="$((cont+1))" 'NR>=a && NR<=b {printf "%4d  %s\n", NR, $0}' "$LOG"
            cp "$LOG" /tmp/jobflake-caught.log
            echo "(saved /tmp/jobflake-caught.log)"
            rm -f "$LOG" "$MON"; exit 1
        fi
    else
        echo "run $run: markers missing (stop=$stop cont=$cont)"
        cp "$LOG" /tmp/jobflake-nocont.log
        echo "--- what happened after the stop ---"
        awk -v a="$stop" 'NR>=a {print NR": "$0}' "$LOG" | head -14
        rm -f "$LOG" "$MON"; exit 2
    fi
    rm -f "$LOG" "$MON"
done
echo "no failure in ${1:-8} runs"

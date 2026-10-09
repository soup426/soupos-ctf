#!/usr/bin/env bash
# trap-test.sh - smoke (sh's trap) for EXIT and INT (v0.60.82).
#
# Scripts against bash script.sh: EXIT with $? kept, reset, after exit N,
# and the listing. Then soupOS's own: a caller's EXIT smoke is not the
# script's, INT catches a Ctrl-C typed while a script loops (and it goes
# on), without it the script ends, and the prompt's EXIT runs at clockout.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-trap.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/h"
printf '%s\n' "trap 'echo T1:bye \$?' EXIT" 'echo T1:body' 'false' > "$WORK/h/s1.sh"
printf '%s\n' "trap 'echo T2:trap' EXIT" 'trap - EXIT' 'echo T2:body' > "$WORK/h/s2.sh"
printf '%s\n' "trap 'echo T3:exit' EXIT" 'exit 3' 'echo T3:no' > "$WORK/h/s3.sh"
printf '%s\n' "trap 'echo hi' EXIT" "trap '' INT" 'trap | while read -r l ; do echo "T4:$l" ; done' > "$WORK/h/s4.sh"
printf '%s\n' "smoke 'slurp T6:caught' INT" 'while [ 1 = 1 ] ; do n=1 ; done' 'slurp T6:after' > "$WORK/s6.sh"
printf '%s\n' 'while [ 1 = 1 ] ; do n=1 ; done' 'slurp T7:after' > "$WORK/s7.sh"
printf '%s\n' 'slurp T5:script-ran' > "$WORK/s5.sh"
for k in 1 2 3 4; do
    sed -e 's/\becho\b/slurp/g' -e 's/\btrap\b/smoke/g' -e 's/\bfalse\b/[ 1 = 2 ]/' -e 's/\bread -r\b/take -r/' "$WORK/h/s$k.sh" > "$WORK/S$k.SH"
    mcopy -i "$WORK/disk.img" "$WORK/S$k.SH" "::/s$k.sh"
done
mcopy -i "$WORK/disk.img" "$WORK/s5.sh" ::/s5.sh; mcopy -i "$WORK/disk.img" "$WORK/s6.sh" ::/s6.sh; mcopy -i "$WORK/disk.img" "$WORK/s7.sh" ::/s7.sh
keys=("headchef" "rosemary" "WAIT:1"
  'follow /s1.sh ; slurp T1:after:$?' "UNTIL:@soupOS:"
  'follow /s2.sh' "UNTIL:@soupOS:"
  'follow /s3.sh ; slurp T3:status:$?' "UNTIL:@soupOS:"
  'follow /s4.sh' "UNTIL:@soupOS:"
  "smoke 'slurp T5:prompt-exit' EXIT" "UNTIL:@soupOS:"
  'follow /s5.sh ; smoke | while take -r l ; do slurp "T5:$l" ; done' "UNTIL:@soupOS:"
  'follow /s6.sh' "WAIT:2" "KEY:ctrl-c" "UNTIL:@soupOS:"
  'follow /s7.sh' "WAIT:2" "KEY:ctrl-c" "UNTIL:@soupOS:"
  'slurp T7:status:$?' "UNTIL:@soupOS:"
  'clockout' "UNTIL:clocks out"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^T[1-4]:' | sed -e 's/^T4:smoke/T4:trap/' -e "s/'slurp hi'/'echo hi'/" > "$WORK/got"
{ bash "$WORK/h/s1.sh"; echo "T1:after:$?"; bash "$WORK/h/s2.sh"; bash "$WORK/h/s3.sh"; echo "T3:status:$?"; bash "$WORK/h/s4.sh"; } 2>/dev/null | grep -E '^T[1-4]:' \
    | grep -vE "^T4:trap -- '' SIG(QUIT|TERM|HUP|TSTP|TTIN|TTOU)\$" > "$WORK/want"   # a background bash inherits these ignored
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    a script's smoke does what bash's trap does ($(wc -l < "$WORK/want") lines: EXIT with \$? kept, reset, after exit 3 with its status, the listing)"
else echo "  FAIL  smoke differs from bash's trap:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
want_own="T5:script-ran T5:smoke -- 'slurp T5:prompt-exit' EXIT T6:caught T6:after T7:status:130 T5:prompt-exit "
own=$(tr -d '\r' < "$WORK/serial.log" | grep -E '^T[5-7]:' | tr '\n' ' ')
if [ "$own" = "$want_own" ]; then echo "  ok    the caller's EXIT stays the caller's, INT catches a Ctrl-C and the script goes on, without it the script stops (130), clockout runs the prompt's EXIT"
else echo "  FAIL  soupOS's own: got '$own'"; echo "                     want '$want_own'"; fail=1; fi
exit $fail

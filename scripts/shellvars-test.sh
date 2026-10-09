#!/usr/bin/env bash
# shellvars-test.sh - $$, $PPID, $SECONDS and $LINENO (v0.60.95): $$ the
# same twice and a number, $PPID 0 on the console, SECONDS=N counting on
# from N past sleeptest, and $LINENO in a script against bash's. $$ $? $#
# and $1 in $(( )) too.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-shellvars.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf '%s\n' 'echo L:$LINENO' '# a comment' 'echo L:$LINENO' '' 'echo L:$LINENO ; echo L:$((LINENO + 10))' 'f() { echo L:in-f ; }' 'f' 'echo L:$LINENO' > "$WORK/l.sh"
sed -e 's/\becho\b/slurp/g' "$WORK/l.sh" > "$WORK/L.SH"; mcopy -i "$WORK/disk.img" "$WORK/L.SH" ::/l.sh
keys=("headchef" "rosemary" "WAIT:1"
  'slurp V1:$$:$(slurp $$):$(( $$ + 1 - 1 ))' "UNTIL:@soupOS:"
  'f() { slurp V0:$(( $# * 10 + $1 )):$(( $? )) ; } ; f 7 x' "UNTIL:@soupOS:"
  'slurp V2:$PPID' "UNTIL:@soupOS:"
  'SECONDS=100 ; slurp V3:$SECONDS' "UNTIL:@soupOS:"
  'sleeptest ; sleeptest ; sleeptest' "UNTIL:@soupOS:"
  'slurp V4:$SECONDS:$(( SECONDS >= 101 ))' "UNTIL:@soupOS:"
  'follow /l.sh' "UNTIL:@soupOS:"
  'slurp V5:$LINENO' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
L=$(tr -d '\r' < "$WORK/serial.log")
v1=$(echo "$L" | grep -E '^V1:' | head -1)
if [[ "$v1" =~ ^V1:([0-9]+):([0-9]+):([0-9]+)$ ]] && [ "${BASH_REMATCH[1]}" = "${BASH_REMATCH[2]}" ] && [ "${BASH_REMATCH[1]}" = "${BASH_REMATCH[3]}" ]; then
    echo "  ok    \$\$ is a number, the same in \$( ) and in \$(( )) ($v1; the console shell is the kernel's first task, 0)"
else echo "  FAIL  \$\$: $v1"; fail=1; fi
echo "$L" | grep -x 'V0:27:0' >/dev/null && echo "  ok    \$# \$1 \$? in arithmetic" || { echo "  FAIL  \$# \$1 \$? in arithmetic: $(echo "$L" | grep '^V0:')"; fail=1; }
echo "$L" | grep -x 'V2:0' >/dev/null && echo "  ok    \$PPID is 0 on the console (the kernel started it)" || { echo "  FAIL  \$PPID: $(echo "$L" | grep '^V2:')"; fail=1; }
echo "$L" | grep -x 'V3:100' >/dev/null && echo "  ok    SECONDS=100 reads back 100" || { echo "  FAIL  SECONDS=100: $(echo "$L" | grep '^V3:')"; fail=1; }
if echo "$L" | grep -E '^V4:10[1-9]:1$' >/dev/null; then echo "  ok    and counts on past 1.5 s of sleeptest ($(echo "$L" | grep '^V4:')), \$((SECONDS)) too"
else echo "  FAIL  SECONDS after sleeptest: $(echo "$L" | grep '^V4:')"; fail=1; fi
echo "$L" | grep -E '^L:' > "$WORK/got"
bash "$WORK/l.sh" | grep -E '^L:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    \$LINENO counts a script's lines as bash's does ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  \$LINENO differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

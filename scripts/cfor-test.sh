#!/usr/bin/env bash
# cfor-test.sh - for (( INIT ; COND ; STEP )) against bash (v0.60.136):
# up and down, -=, an empty COND with break, continue (STEP still runs),
# nested and break 2, the status (0 when the body never ran), a bare name
# and a $var in the parts, for(( with no space, ((;;)), and in a recipe
# over several lines.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cfor.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'for (( k=1; k<=3; k++ ))\ndo\n  slurp R1:$k\ndone\nslurp R2:$k\n' > "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
LINES=(
  'for (( i=0; i<3; i++ )) ; do slurp F1:$i ; done'
  'for ((i=10; i>0; i-=3)); do slurp F2:$i; done'
  'for (( i=0; ; i++ )) ; do [ $i -ge 2 ] && break ; slurp F3:$i ; done'
  'for (( i=0; i<5; i++ )) ; do [ $i = 2 ] && continue ; slurp F4:$i ; done ; slurp F4e:$i'
  'for ((a=1;a<=2;a++)); do for ((b=1;b<=3;b++)); do [ $b = 3 ] && continue 2 ; slurp F5:$a$b; done; done'
  'for ((a=1;a<=3;a++)); do for ((b=1;b<=3;b++)); do [ $a$b = 22 ] && break 2 ; slurp F6:$a$b; done; done'
  'for ((i=0;i<0;i++)); do spoiled; done; slurp F7:$?'
  'for ((i=0;i<1;i++)); do spoiled; done; slurp F8:$?'
  'n=3 ; for ((i=n; i>0; i--)); do slurp F9:$i; done'
  'for ((;;)) ; do slurp F10:once ; break ; done'
  'for((i=0;i<2;i++)); do slurp F11:$i; done'
  'm=2 ; for (( i=0; i<$m; i++ )) ; do slurp F12:$i ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('follow /r.sh' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[FR][0-9]+e?:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//spoiled/false}; script+="$b"$'\n'; done
sed 's/slurp/echo/' "$H/r.sh" > "$H/r.bash"; script+="source $H/r.bash"$'\n'
bash -c "$script" 2>/dev/null | grep -E '^[FR][0-9]+e?:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    for ((;;)) is bash's ($(wc -l < "$WORK/want") lines: up, down, -=, break, continue, nested with 2, status, a name and \$var, for((, ((;;)), a recipe over lines)"
else echo "  FAIL  for ((;;)) differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

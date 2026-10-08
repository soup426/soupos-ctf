#!/usr/bin/env bash
# cd-test.sh - cd -, $PWD and $OLDPWD against bash (v0.60.32).
#
# /, /etc and /home exist in soupOS and on the host, so the same lines run
# in both; bash starts in / with HOME=/ (the headchef's home) and no OLDPWD.
# The D: lines each prints must match; cd -'s own line is checked apart
# (soupOS indents it as pwd does).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cd.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mmd -i "$WORK/disk.img" ::/home 2>/dev/null    # bash has one; soupOS makes it at the first hire
LINES=(
  'slurp D0:$PWD:$OLDPWD'
  'cd /etc ; cd /home ; slurp D1:$PWD:$OLDPWD'
  'cd - ; slurp D2:$PWD:$OLDPWD'
  'cd /nope ; slurp D3:$?:$PWD'
  'PWD=mine ; slurp D4:$PWD ; cd /home ; slurp D5:$PWD:$OLDPWD'
  'discard OLDPWD ; cd - ; slurp D6:$?:$PWD'
  'cd /etc ; cd ; slurp D7:$PWD:$OLDPWD'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
grep -E '^D[0-9]:' "$WORK/out.log" > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//discard/unset}; script+="$b"$'\n'; done
( cd / && env -i HOME=/ PATH="$PATH" bash -c "$script" 2>/dev/null ) | grep -E '^D[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(tr '\n' ' ' < "$WORK/want"))"
else echo "  FAIL  cd differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
grep -A1 -F '> cd - ; slurp D2' "$WORK/out.log" | tail -1 | grep -x '  /etc' >/dev/null \
    && echo "  ok    cd - says where it went" || { echo "  FAIL  cd - did not print /etc"; fail=1; }
grep -q 'cd: OLDPWD not set' "$WORK/out.log" && echo "  ok    cd - with no OLDPWD says so" \
    || { echo "  FAIL  no 'OLDPWD not set'"; fail=1; }
exit $fail

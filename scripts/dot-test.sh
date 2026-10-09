#!/usr/bin/env bash
# dot-test.sh - . FILE against bash's . (v0.60.91): in this shell (what it
# sets and defines stays), its own arguments or else the caller's (a shift
# there is the caller's), return leaving the file and not the function
# that ran it, and an EXIT trap it sets being the shell's.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-dot.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/b"
printf '%s\n' 'v=set-by-file' 'echo D1:args:$#:$1' 'f() { echo D2:fn-from-file ; }' 'shift' 'return 3' 'echo D3:no' > "$WORK/b/s.sh"
printf '%s\n' "trap 'echo D9:at-exit' EXIT" > "$WORK/b/t.sh"
for f in s t; do sed -e 's/\becho\b/slurp/g' -e 's/\btrap\b/smoke/g' "$WORK/b/$f.sh" > "$WORK/$f.SH"; mcopy -i "$WORK/disk.img" "$WORK/$f.SH" "::/$f.sh"; done
LINES=(
  '. /s.sh a b ; slurp D4:$?:$v:[$1]'
  'f ; slurp D5:$?'
  'h() { . /s.sh ; slurp "D6:$?:$#:$1" ; } ; h p q r'
  '. /t.sh ; smoke | while take -r l ; do slurp "D8:$l" ; done'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=("clockout" "UNTIL:clocks out")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^D[0-9]+:' | sed -e 's/^D8:smoke /D8:trap /' -e 's/slurp D9/echo D9/' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//smoke/trap}; b=${b//take/read}; b=${b//\/s.sh/$WORK\/b\/s.sh}; b=${b//\/t.sh/$WORK\/b\/t.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^D[0-9]+:' \
    | grep -vE "^D8:trap -- '' SIG(INT|QUIT|TERM|HUP|TSTP|TTIN|TTOU)\$" > "$WORK/want"   # a background bash inherits these ignored
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    . does what bash's does ($(wc -l < "$WORK/want") lines: sets and functions stay, its own \$1, the caller's when none, shift and return there, an EXIT trap the shell's)"
else echo "  FAIL  . differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

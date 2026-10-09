#!/usr/bin/env bash
# promptdoc-test.sh - here-documents typed at the prompt (v0.60.68): a >
# prompt for each line up to WORD; $X expanded unless WORD is quoted;
# Ctrl-C drops the line (status 130); Ctrl-D on an empty line ends the
# document there, with bash's warning; the prompt is itself afterwards;
# and << inside $(( )) is a shift, not a document.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-promptdoc.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
keys=("headchef" "rosemary" "WAIT:1"
  'v=hi ; cook spoon.elf <<EOF' 'H1:line one' 'H2:v is $v and $((2+3))' 'EOF' "UNTIL:@soupOS:"
  "cook spoon.elf <<'END'" 'H3:$v stays' 'END' "UNTIL:@soupOS:"
  'cook spoon.elf <<EOF' 'H4:never' "KEY:ctrl-c" "UNTIL:@soupOS:"
  'slurp H5:$?' "UNTIL:@soupOS:"
  'cook spoon.elf <<EOF' 'H6:partial' "KEY:ctrl-d" "UNTIL:@soupOS:"
  'slurp H7:after' "UNTIL:@soupOS:"
  'slurp H8:$(( 1 << 3 ))' "UNTIL:@soupOS:"
)
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^H[0-9]:' > "$WORK/got"
printf '%s\n' 'H1:line one' 'H2:v is hi and 5' 'H3:$v stays' 'H5:130' 'H6:partial' 'H7:after' 'H8:8' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    typed here-documents feed the cook ($(wc -l < "$WORK/want") lines: expanded, quoted WORD kept as typed, Ctrl-C drops it with 130, Ctrl-D ends it early, the prompt after, and << in \$(( )) is a shift, not a document)"
else echo "  FAIL  typed here-documents:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
if tr -d '\r' < "$WORK/serial.log" | grep -F "here-document delimited by end-of-file (wanted \`EOF')" >/dev/null; then echo "  ok    Ctrl-D says the document ended early, as bash does"
else echo "  FAIL  no warning on Ctrl-D"; fail=1; fi
if tr -d '\r' < "$WORK/serial.log" | grep -F '> H1:line one' >/dev/null; then echo "  ok    each document line is asked for with a > prompt"
else echo "  FAIL  no > prompt"; fail=1; fi
exit $fail

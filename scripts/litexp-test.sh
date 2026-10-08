#!/usr/bin/env bash
# litexp-test.sh - what comes out of an expansion is text, against bash (v0.60.39).
#
# Quotes, | > & in a value: sh never re-reads them, so `v="it's" ; echo $v`
# prints it's, and a > in $v makes no file. The X: lines both shells print
# must match, and /Y.TXT must not exist.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-litexp.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  "v=\"it's\" ; slurp X1:\$v"
  "v='say \"hi\"' ; slurp \"X2:\$v\""
  "v='a|b' ; cook call.elf X3:\$v"
  "v='x > Y.TXT' ; cook call.elf X4:\$v"
  "v=\"it's\" ; cook call.elf \"X5:\$v\""
  "v=\"don't\" ; for w in \$v ok ; do slurp X6:\$w ; done"
  "x=\$(cook call.elf \"q'q\") ; slurp X7:\$x"
  "v='a&&b' ; slurp X8:\$v"
  "v=\"it's\" ; w=\$v ; slurp X9:\$w"
  "v=\"it's\" ; case \$v in it*) slurp X10:yes ;; esac"
  "v='\"' ; slurp X11:\$v\$v"
  "v=\"it's\" ; hand v ; cook labels.elf v | while take z ; do slurp \"X12:\$z\" ; done"
  "slurp X13:\$(cook call.elf \"a'b\" \"c|d\")"
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^X[0-9]+:' > "$WORK/got"
mkdir -p "$WORK/host"
script=""
for l in "${LINES[@]}"; do
    b=${l//cook call.elf/echo}; b=${b//cook labels.elf v/printenv v}; b=${b//cook spoon.elf \//cat }
    b=${b//slice.elf -c 1-9/cut -c 1-9}; b=${b//take/read}; b=${b//hand/export}; b=${b//slurp/echo}; b=${b//> \//> }
    script+="$b"$'\n'
done
( cd "$WORK/host" && bash -O lastpipe -c "$script" ) | grep -E '^X[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints (quotes, | > & from \$v, \"\$v\", \$(...), for, an assignment, case, hand)"
else echo "  FAIL  expanded text differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
mdir -b -i "$WORK/disk.img" ::/Y.TXT >/dev/null 2>&1 && { echo "  FAIL  a > that came out of \$v made a file"; fail=1; } \
    || echo "  ok    a > that came out of \$v is text, not a redirection"
exit $fail

#!/usr/bin/env bash
# bihere-test.sh - <<< WORD and <<WORD for builtins, functions and loops,
# against bash (v0.60.80). Also: a document goes to the command it is on,
# not the first cook of the line.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bihere.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
# each entry: the typed lines of one command, | between lines
CMDS=(
  'take a b <<< "x y z" ; slurp "H1:[$a][$b]:$?"'
  'w=hi ; take a <<< $w ; slurp H2:$a'
  'while take l ; do slurp H3:$l ; done <<< one'
  'take a b <<EOF|first second third|EOF'
  'slurp "H4:[$a][$b]"'
  'while take l ; do slurp H5:$l ; done <<EOF|l1|l2 $w|EOF'
  'f() { take q ; slurp H6:$q ; } ; f <<< fn'
  'cook call.elf H7:first ; cook spoon.elf <<EOF|H7:doc|EOF'
  'if take a <<< yes ; then slurp H8:$a ; fi'
  'take a <<< "" ; slurp "H9:[$a]:$?"'
)
keys=("headchef" "rosemary" "WAIT:1")
script=""
for c in "${CMDS[@]}"; do
    IFS='|' read -r -a parts <<< "$c"
    for x in "${parts[@]}"; do keys+=("$x"); done
    keys+=("UNTIL:@soupOS:")
    b=$(printf '%s\n' "${parts[@]}")
    b=${b//cook spoon.elf/cat}; b=${b//cook call.elf/echo}; b=${b//slurp/echo}; b=${b//take/read}
    script+="$b"$'\n'
done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^H[0-9]+:' > "$WORK/got"
bash -c "$script" 2>/dev/null | grep -E '^H[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    builtins read <<< and <<WORD as bash's do ($(wc -l < "$WORK/want") lines: take, \$w, a while loop fed both ways, typed documents, a function, the second cook of a line, an if condition, an empty string)"
else echo "  FAIL  builtins' here-strings and documents differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

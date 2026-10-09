#!/usr/bin/env bash
# heredoc-test.sh - here-documents in a follow script, against bash (v0.60.52).
#
# One script on the disk, run by follow with an argument; bash runs the same
# script (cook X as its host tool, slurp as echo) and every file the two
# write must match.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-heredoc.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
cat > "$WORK/H1.SH" <<'EOF'
NAME=world
cook raise.elf > /O1.TXT <<EOT
hello $NAME
it's ${NAME}!
EOT
cook rack.elf > /O2.TXT <<'END'
pear
$NAME
apple
END
cook swap.elf a-z A-Z > /O3.TXT <<X
first $1 and status $?
X
cook weigh.elf -l > /O4.TXT <<EOT
one
two
three
EOT
cook call.elf after > /O5.TXT
n=7 ; cook raise.elf > /O6.TXT <<EOT
n is $n, twice $((n * 2)), $((n > 5 ? 1 : 0))
EOT
EOF
mcopy -i "$WORK/disk.img" "$WORK/H1.SH" ::/H1.SH
sed -e 's#cook raise.elf#tr a-z A-Z#; s#cook rack.elf#sort#; s#cook swap.elf#tr#; s#cook weigh.elf -l#wc -l#; s#cook call.elf#echo#; s# > /# > #' \
    "$WORK/H1.SH" > "$WORK/host/H1.SH"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /H1.SH arg1" "UNTIL:@soupOS:" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
( cd "$WORK/host" && LC_ALL=C bash H1.SH arg1 ) >/dev/null 2>&1
bad=0
for f in O1 O2 O3 O4 O5 O6; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    if [ "$f" = O4 ]; then [ "$(tr -d ' ' < "$WORK/got.$f")" = "$(tr -d ' ' < "$WORK/host/$f.TXT")" ] && continue
    else cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" && continue; fi
    echo "        $f.TXT: got '$(tr '\n' '|' < "$WORK/got.$f")', bash '$(tr '\n' '|' < "$WORK/host/$f.TXT")'"; bad=1
done
[ "$bad" = 0 ] && echo "  ok    a script's five here-documents give what bash's give (\$NAME and \${NAME} expanded, <<'END' left alone, \$1 and \$?, the line after still runs, a variable set on the same line, \$(( )))" \
    || { echo "  FAIL  here-documents differ from bash"; fail=1; }
exit $fail

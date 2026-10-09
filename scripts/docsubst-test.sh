#!/usr/bin/env bash
# docsubst-test.sh - $( ) in here-documents, against bash (v0.60.70): its
# output with its newlines, a cook inside it not taking the outer cook's
# document, nested, and left alone under a quoted WORD.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-docsubst.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/host"
printf 'l1\nl2\n' > "$WORK/host/two.txt"
cat > "$WORK/S.SH" <<'S'
v=one
cook spoon.elf > /D1.TXT <<EOT
a $(slurp hi $v)
b $(cook spoon.elf /two.txt)
c $(cook call.elf x $(slurp y))
d $((1+1)) $v
EOT
cook spoon.elf > /D2.TXT <<'EOT'
e $(pwd) $v
EOT
S
mcopy -i "$WORK/disk.img" "$WORK/S.SH" ::/S.SH
mcopy -i "$WORK/disk.img" "$WORK/host/two.txt" ::/two.txt
sed -e 's#cook spoon.elf#cat#g; s#cook call.elf#echo#g; s#slurp#echo#g; s# > /# > #; s#/two.txt#two.txt#' "$WORK/S.SH" > "$WORK/host/S.SH"
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /S.SH" "UNTIL:@soupOS:" \
    'cook spoon.elf <<EOF' 'P1:$(slurp typed) $(cook call.elf too)' 'EOF' "UNTIL:@soupOS:" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
( cd "$WORK/host" && bash S.SH ) >/dev/null 2>&1
bad=0
for f in D1 D2; do
    mcopy -n -i "$WORK/disk.img" "::/$f.TXT" "$WORK/got.$f" 2>/dev/null || { echo "        $f.TXT was not written"; bad=1; continue; }
    cmp -s "$WORK/got.$f" "$WORK/host/$f.TXT" && continue
    echo "        $f.TXT: got '$(tr '\n' '|' < "$WORK/got.$f")', bash '$(tr '\n' '|' < "$WORK/host/$f.TXT")'"; bad=1
done
[ "$bad" = 0 ] && echo "  ok    a script's documents with \$( ) are bash's (a builtin, a program's two lines, nested, beside \$(( )) and \$v, <<'EOT' left alone)" \
    || { echo "  FAIL  \$( ) in here-documents differs from bash"; fail=1; }
if tr -d '\r' < "$WORK/serial.log" | grep -x 'P1:typed too' >/dev/null; then echo "  ok    typed at the prompt too"
else echo "  FAIL  typed: $(tr -d '\r' < "$WORK/serial.log" | grep '^P1' | head -1)"; fail=1; fi
exit $fail

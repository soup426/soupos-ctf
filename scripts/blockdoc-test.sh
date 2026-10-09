#!/usr/bin/env bash
# blockdoc-test.sh - here-documents inside a script's multi-line blocks,
# against bash (v0.60.83): in a for (expanded each time round), after a
# while's done, in an if with a quoted WORD and on a builtin, in a function
# defined over lines and called later.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-blockdoc.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/b.sh" <<'S'
for i in 1 2
do
  cat <<EOF
D1:line $i
EOF
done
while read l
do
  echo D2:$l
done <<EOF
one
two
EOF
if true
then
  cat <<'X'
D3:$i kept
X
  read a <<EOF
D4:$i
EOF
  echo $a
fi
f() {
  cat <<EOF
D5:in f $1
EOF
}
f arg
f again
echo D6:after
S
sed -e 's/\becho\b/slurp/g' -e 's/\bcat\b/cook spoon.elf/g' -e 's/\bread\b/take/g' -e 's/^if true$/if [ 1 = 1 ]/' "$WORK/b.sh" > "$WORK/B.SH"
mcopy -i "$WORK/disk.img" "$WORK/B.SH" ::/b.sh
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /b.sh" "UNTIL:@soupOS:" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^D[0-9]:' > "$WORK/got"
bash "$WORK/b.sh" 2>/dev/null | grep -E '^D[0-9]:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    documents in a script's blocks are bash's ($(wc -l < "$WORK/want") lines: in a for each time round, after done, <<'X' in an if, on take, in a function called twice)"
else echo "  FAIL  documents in blocks differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

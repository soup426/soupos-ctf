#!/usr/bin/env bash
# multiline-test.sh - if, loops, case and functions over several lines in a
# follow script, against bash (v0.60.74): follow joins a compound's lines.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-multiline.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/m.sh" <<'S'
# a comment before
for i in 1 2
do
  echo M1:$i
done
n=0
while (( n < 2 ))
do
  n=$((n+1))
  # a comment inside
  if (( n == 1 ))
  then
    echo M2:one
  else
    echo M2:other
  fi
done
case $n in
  1) echo M3:one ;;
  2)
    echo M3:two
    ;;
esac
f() {
  echo M4:in-f $1
  if (( $# > 1 )); then
    echo M4:two-args
  fi
}
f arg
f a b
for x in a b; do echo M5:$x; done
x=$(echo sub) ; echo M6:$x
until (( n == 0 ))
do
  n=$((n-1))
done
echo M7:end:$n
S
sed -e 's/\becho\b/slurp/g' "$WORK/m.sh" > "$WORK/M.SH"
mcopy -i "$WORK/disk.img" "$WORK/M.SH" ::/m.sh
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /m.sh" "UNTIL:@soupOS:" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^M[0-9]+:' > "$WORK/got"
bash "$WORK/m.sh" 2>/dev/null | grep -E '^M[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    a script's compounds over lines run as bash's ($(wc -l < "$WORK/want") lines: for, while with an if inside, case with an arm over lines, a function over lines, one-line forms, \$( ), until, comments)"
else echo "  FAIL  multi-line compounds differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# pluck-test.sh - pluck (sh's getopts) against bash's getopts (v0.60.77).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-pluck.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/s.sh" <<'S'
while getopts "vn:" o
do
  case $o in
    v) echo S1:verbose ;;
    n) echo S1:n=$OPTARG ;;
    *) echo S1:bad ;;
  esac
done
shift $((OPTIND-1))
echo S2:rest:$@
S
sed -e 's/\becho\b/slurp/g' -e 's/\bgetopts\b/pluck/g' "$WORK/s.sh" > "$WORK/S.SH"
mcopy -i "$WORK/disk.img" "$WORK/S.SH" ::/s.sh
LINES=(
  'f() { while pluck "ab:c" o ; do slurp "P1:$o:${OPTARG}:$OPTIND" ; done ; slurp "P2:$OPTIND" ; shift $((OPTIND-1)) ; slurp "P3:$@" ; } ; f -a -b val -c rest more'
  'OPTIND=1 ; f -acb x y'
  'OPTIND=1 ; f -b'
  'OPTIND=1 ; f -a -- -b'
  'OPTIND=1 ; f -bfoo x'
  'OPTIND=1 ; f plain -a'
  'OPTIND=1 ; f -z -a'
  'g() { OPTIND=1 ; while pluck ":ab:" o ; do slurp "P4:$o:${OPTARG}" ; done ; } ; g -x -b'
  'OPTIND=1 ; pluck "a" o -a ; slurp "P5:$o:$?:$OPTIND" ; pluck "a" o -a ; slurp "P6:$o:$?:$OPTIND"'
  'follow /s.sh -v -n 3 file1 file2'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[PS][0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//pluck/getopts}; b=${b//slurp/echo}; b=${b//follow \/s.sh/bash $WORK\/s.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[PS][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    pluck does what bash's getopts does ($(wc -l < "$WORK/want") lines: options and their arguments, -acb, -bfoo, a missing argument, --, a plain word, an unknown letter, the quiet form, ARGS given, OPTIND, a script with shift)"
else echo "  FAIL  pluck differs from bash's getopts:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# atstar-test.sh - "$@", $@, "$*" and $* against bash (v0.60.78), with an
# argument holding a space and with none: for lists, a program's
# arguments, assignments, "x$@y", $#, a here-document, a script.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-atstar.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/s.sh" <<'S'
for x in "$@" ; do echo "S1:[$x]" ; done
printf "S2:<%s>\n" "$@"
y="$*" ; echo "S3:[$y]:$#"
cat <<EOF
S4:$# $@ $*
EOF
S
sed -e 's/\becho\b/slurp/g' -e 's/^printf /cook dish.elf /' -e 's/^cat /cook spoon.elf /' "$WORK/s.sh" > "$WORK/S.SH"
mcopy -i "$WORK/disk.img" "$WORK/S.SH" ::/s.sh
LINES=(
  'f() { for x in "$@" ; do slurp "A1:[$x]" ; done ; } ; f "a b" c'
  'f() { for x in $@ ; do slurp "A2:[$x]" ; done ; } ; f "a b" c'
  'f() { for x in "$*" ; do slurp "A3:[$x]" ; done ; } ; f "a b" c'
  'f() { for x in $* ; do slurp "A4:[$x]" ; done ; } ; f "a b" c'
  'f() { cook dish.elf "A5:<%s>\n" "$@" ; } ; f "a b" c'
  'f() { cook dish.elf "A6:<%s>\n" "x$@y" ; } ; f "a b" c'
  'f() { y="$@" ; z=$* ; slurp "A7:[$y][$z]" ; } ; f "a b" c'
  'f() { for x in "$@" ; do slurp "A8:[$x]" ; done ; slurp A8:end:$# ; } ; f'
  'f() { for x in "$*" ; do slurp "A9:[$x]" ; done ; } ; f'
  'follow /s.sh "p q" r'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^[AS][0-9]+:' > "$WORK/got"
script=""
for l in "${LINES[@]}"; do b=${l//cook dish.elf/printf}; b=${b//cook spoon.elf/cat}; b=${b//slurp/echo}; b=${b//follow \/s.sh/bash $WORK\/s.sh}; script+="$b"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^[AS][0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    \"\$@\" and \"\$*\" are bash's ($(wc -l < "$WORK/want") lines: quoted and bare in for, as a program's arguments, \"x\$@y\", after =, none at all, a here-document, a script)"
else echo "  FAIL  \$@ or \$* differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

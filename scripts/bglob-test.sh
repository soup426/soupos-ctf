#!/usr/bin/env bash
# bglob-test.sh - globs in builtins' and functions' words, against bash
# (v0.60.130): * ? [...], no match left as it is, quoted and \ ones left
# alone, a glob in a variable (globbed bare, not in "..."), NAME=value not
# globbed, a function's arguments, temper --, [ ] not taken for a glob,
# dot files only by a leading dot, and a 302-name glob that does not fit
# the line refused (status 1) instead of passed on as the pattern.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bglob.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H/g" "$H/big"
for f in a1.txt a2.txt b.txt "c d.txt" .hidden; do echo x > "$H/g/$f"; done
for i in $(seq 302); do echo $i > "$H/big/f$i.txt"; done
mmd -i "$WORK/disk.img" ::/g ::/big
mcopy -i "$WORK/disk.img" "$H"/g/* "$H/g/.hidden" ::/g/
mcopy -i "$WORK/disk.img" "$H"/big/* ::/big/
LINES=(
  'cd /g ; slurp G1: *.txt'
  'slurp G2: a?.txt ; slurp G3: [ab]*.txt'
  'slurp G4: nomatch* ; slurp G5: "*.txt" '"'"'*.txt'"'"' \*.txt'
  'x="a*" ; slurp G6: $x ; slurp G7: "$x"'
  'y=*.txt ; slurp G8: $y ; slurp "G9: $y"'
  'f() { slurp G10: $# "$1" ; } ; f *.txt'
  '[ -f a1.txt ] && slurp G11: ok ; [[ -f b.txt ]] && slurp G12: ok'
  'temper -- *.txt ; slurp G13: $# ; temper --'
  'slurp G14: * ; slurp G15: .h*'
  'slurp G16: /g/b* ; cd /'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
keys+=('slurp /big/* ; slurp B:$?' "UNTIL:@soupOS:" 'for f in /big/* ; do slurp "$f" ; done | cook weigh.elf -l > /n.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/log"
grep -E '^G[0-9]+:' "$WORK/log" > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//slurp/echo}; b=${b//temper/set}; b=${b//cd \/g/cd $H\/g}; b=${b//\/g\/b\*/$H\/g\/b*}; script+="$b"$'\n'; done
(cd "$H" && LC_ALL=C bash -c "$script" 2>/dev/null) | grep -E '^G[0-9]+:' | sed "s|$H||g" > "$WORK/want"
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; bad=1; }
grep -x 'B:1' "$WORK/log" >/dev/null && grep 'too long once expanded' "$WORK/log" >/dev/null || { echo "        the 302-name glob was not refused with status 1"; bad=1; }
grep -x '/big/\*' "$WORK/log" >/dev/null && { echo "        the 302-name glob was passed on as the pattern"; bad=1; }
mcopy -n -i "$WORK/disk.img" ::/n.txt "$WORK/n" 2>/dev/null; [ "$(tr -d ' ' < "$WORK/n")" = 302 ] || { echo "        a for loop's glob gave $(cat "$WORK/n") of 302"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    builtins and functions glob as bash ($(wc -l < "$WORK/want") lines: * ? [], no match, quoted, a variable bare and quoted, NAME=, a function, temper --, [ ], dot files, a path; 302 names refused, not passed on)" \
    || { echo "  FAIL  builtin globs differ from bash"; fail=1; }
exit $fail

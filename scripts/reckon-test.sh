#!/usr/bin/env bash
# reckon-test.sh - reckon.elf (expr) against GNU expr, output and status
# (v0.60.124): arithmetic and precedence, comparisons as numbers and as
# text, | and & with null and 0, length, substr, index, match, + TOKEN,
# STRING : REGEX (a basic regex, anchored, with and without \( \)), and
# the errors (2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-reckon.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
cat > "$H/cases" <<'CASES'
1 + 2
10 - 3 '*' 2
7 / 2
7 % 3
-7 / 2
-7 % 3
2 '*' '(' 3 + 4 ')'
2 + 3 '*' 4 = 14
5 '>' 3
abc '<' abd
10 '<' 9
10 '<' 9a
3 = 03
x = x
a '!=' b
4 '>=' 4
0 '|' 5
'' '|' x
0 '|' ''
3 '&' 0
3 '&' 4
length hello
length ''
substr hello 2 3
substr hello 0 2
substr hello 4 10
index hello lo
index hello z
hello : 'h.l'
hello : '\(h.l\)'
hello : x
abc : '.*'
'a+b' : 'a+b'
foo123 : '[a-z]*\([0-9]*\)'
aab : 'a\+'
bbc : 'a\|b'
ab : '\(a\)\(b\)'
abc : 'a*$'
'ab$c' : 'ab$c'
'a*b' : '*b'
aaa : 'a\{2\}'
abc : '\(x\)*'
abc : '^a'
match abc 'a.'
+ length
length + length
1 / 0
1 + a
1 +
'(' 1 + 2
1 2
-0 '|' 7
CASES
k=0; : > "$H/r.sh"
while IFS= read -r c; do k=$((k+1)); printf 'cook reckon.elf %s\nslurp "== %d $?"\n' "$c" "$k" >> "$H/r.sh"; done < "$H/cases"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^cook reckon.elf/expr/; s/^slurp/echo/' "$H/r.sh" > "$H/r.bash"
LC_ALL=C bash "$H/r.bash" 2>/dev/null > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    reckon is GNU expr ($k expressions, output and status alike: arithmetic, comparisons, | &, keywords, + TOKEN, BRE :, errors)"
else echo "  FAIL  reckon differs from expr:"; diff "$WORK/want" "$WORK/got" | head -40 | sed 's/^/        /'; fail=1; fi
exit $fail

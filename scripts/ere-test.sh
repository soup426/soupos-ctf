#!/usr/bin/env bash
# ere-test.sh - sift -E (rx.c, POSIX extended regular expressions) against
# GNU grep -E (v0.60.119): 26 patterns over the same lines, each with -o,
# -c, -v and -in, through one recipe on each side; three patterns that
# take a backtracking matcher exponential time (a Pike VM: linear), and
# six bad ones (status 2; v0.60.319, review R-N59: an unclosed [ inside a
# group compiled to a program matching the empty string everywhere). GNU grep is called by
# its path: an interactive shell's grep may be a ugrep wrapper.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ere.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
cat > "$H/t.txt" <<'TXT'
soup of the day
Soup Of The Day
a cat sat on the concat mat
color colour colouur
foobaz barbaz bazbaz
abababc ababc abc
xx x xxx xxxx
call 555-1234 or 555-9876
the end.
*star* and a{ and b) and ]x[

SOUPSOUP soupsoup
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac
TXT
PATS=('soup|cat' '^s' 't$' 'o+' 'so?up' 'x{2}' 'x{2,}' 'x{1,2}' '[aeiou]{2}' '[^a-z ]+'
      '(ab)+c' 'a.c' '\bcat\b' '[[:digit:]]+' '\w+' '^$' 'colou?r' '(foo|bar)baz' '*star'
      'a{' 'b)' '[]x]+' '\.' '(soup){2}' '[0-9]{3}-[0-9]{4}' 'ab|abab'
      '(a*)*b' '(a|aa)+$' '(a+)+c'
      '\<b' 'z$' 'p\>' '\Ba')   # (v0.60.243) an assertion first, false where the search starts: rx.c stopped there
{
  k=0
  for p in "${PATS[@]}"; do
    k=$((k+1))
    for m in -o -c -v -in; do
      printf "slurp '== %d %s'\n" "$k" "$m"
      printf "cook sift.elf -E %s '%s' /t.txt\n" "$m" "$p"
    done
  done
  for p in '(' 'a(b' '[a' '(a[)' 'x(y[z)' '([[:alpha:])'; do   # bad patterns: status 2, nothing out; (v0.60.319) a [ unclosed inside a group too
    printf "cook sift.elf -E '%s' /t.txt\nslurp \"== bad \$?\"\n" "$p"
  done
} > "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/t.txt" ::/t.txt; mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed "s/^cook sift.elf/\/usr\/bin\/grep/; s/^slurp/echo/; s/ \/t.txt$/ t.txt/" "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash 2>/dev/null) > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    sift -E is GNU grep -E ($(grep -c '^==' "$WORK/want") runs: 33 patterns with -o, -c, -v, -in, 6 bad; $(wc -l < "$WORK/want") lines alike)"
else echo "  FAIL  sift -E differs from grep -E:"; diff "$WORK/want" "$WORK/got" | head -40 | sed 's/^/        /'; fail=1; fi
exit $fail

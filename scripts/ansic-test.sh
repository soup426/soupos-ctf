#!/usr/bin/env bash
# ansic-test.sh - $'...' (bash's ANSI-C quoting) against bash, byte for byte
# (v0.60.126): each escape, \' inside, octal, hex, \e, \cX, an unknown one
# kept, a NUL ending it (bytes 1, 2 and 31 are the shell's markers: not
# tested), in an assignment, a case pattern and [[ ]], no
# other expansion inside, literal inside "...", and joined to other text.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-ansic.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
cat > "$H/r.sh" <<'R'
slurp $'a\nb'
slurp $'tab\there'
slurp $'q\'s and more'
slurp $'dq\"x'
slurp $'oct\101\0102'
slurp $'hex\x41\x4a\x7'
slurp $'esc\e[0m'
slurp $'bs\\x'
slurp $'nul\0after' end
x=$'a\tb' ; slurp "[$x]"
case $'a\nb' in $'a\nb') slurp case-ok ;; *) slurp case-no ;; esac
[[ $'x\ty' == $'x\ty' ]] && slurp dbr-ok
slurp $'no $HOME $(nothing) `here` expansion'
slurp "$'not ansi in dq'"
slurp $'unknown \q esc'
slurp pre$'mid\tdle'post
slurp $'ctrl\cG\cz'
slurp $'a;b|c&d'
R
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:"
      "slurp \$'typed\\'s\\tline!' > /typed.txt" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/ slurp / echo /g' "$H/r.sh" > "$H/r.bash"
bash "$H/r.bash" > "$WORK/want" 2>/dev/null
# and typed at the prompt, where history expansion and the > prompt read the line first
mcopy -n -i "$WORK/disk.img" ::/typed.txt "$WORK/got.t" 2>/dev/null || touch "$WORK/got.t"
printf "typed's\tline!\n" > "$WORK/want.t"
cmp -s "$WORK/got.t" "$WORK/want.t" || { echo "  FAIL  typed at the prompt: $(od -c "$WORK/got.t" | head -2)"; fail=1; }
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    \$'...' is bash's ($(wc -l < "$H/r.sh") lines alike byte for byte: escapes, \\', octal, hex, \\e, \\c, NUL, assignment, case, [[ ]], nothing expanded, in \"...\", joined; and typed)"
else echo "  FAIL  \$'...' differs from bash:"; diff <(od -c "$WORK/want") <(od -c "$WORK/got") | head -20 | sed 's/^/        /'; fail=1; fi
exit $fail

#!/usr/bin/env bash
# siftfiles-test.sh - sift over several files, -l -H -h -w -x, against GNU
# grep (by its path: an interactive grep may be a ugrep wrapper) (v0.60.134):
# NAME: prefixes with -n -c -o, -l and -lv, -w with words inside words,
# -x, both with -E, a missing file among them (status 2), and stdin.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-siftfiles.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
printf 'soup of the day\nbread\nmore soup soup\n' > "$H/a.txt"
printf 'no match here\nSoup in caps\nsouper\n' > "$H/b.txt"
printf 'nothing\nat all\n' > "$H/c.txt"
printf 'cat\nconcat\ncat_ and cat.\nthe cat sat\nexact line\nCAT nap\ndog\n' > "$H/w.txt"
for f in a.txt b.txt c.txt w.txt; do mcopy -i "$WORK/disk.img" "$H/$f" "::/$f"; done
CASES=("soup a.txt b.txt" "-n soup a.txt b.txt" "-c soup a.txt b.txt c.txt" "-l soup a.txt b.txt c.txt"
       "-lv soup a.txt b.txt c.txt" "-h soup a.txt b.txt" "-H soup a.txt" "-on soup a.txt b.txt"
       "-w cat w.txt" "-wo cat w.txt" "-x 'exact line' w.txt" "-E -x 'cat|dog' w.txt" "-E -w 'ca[a-z]' w.txt"
       "-iw cat w.txt" "-ic soup a.txt b.txt" "-w soup a.txt b.txt" "-l soup a.txt nope.txt b.txt")
k=0; : > "$H/r.sh"
for c in "${CASES[@]}"; do k=$((k+1)); printf 'slurp "== %d"\ncook sift.elf %s\nslurp "st $?"\n' $k "$c" >> "$H/r.sh"; done
printf 'slurp "== in"\ncook spoon.elf a.txt | cook sift.elf -l soup\ncook spoon.elf a.txt | cook sift.elf -c soup\n' >> "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'cd / ; follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^slurp /echo /; s/cook sift.elf/\/usr\/bin\/grep/; s/cook spoon.elf/cat/' "$H/r.sh" > "$H/r.bash"
(cd "$H" && LC_ALL=C bash r.bash 2>/dev/null) > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    sift over several files is GNU grep ($k cases and stdin, $(wc -l < "$WORK/want") lines with statuses: NAME:, -n -c -o, -l -lv, -H -h, -w -x with and without -E, a missing file 2)"
else echo "  FAIL  sift differs from grep:"; diff "$WORK/want" "$WORK/got" | head -30 | sed 's/^/        /'; fail=1; fi
exit $fail

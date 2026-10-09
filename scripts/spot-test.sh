#!/usr/bin/env bash
# spot-test.sh - spot.elf (diff) against GNU diff's normal format and
# status (v0.60.122): the same files, changes at the start, middle and
# end, insertions, deletions, several hunks, empty files, a missing last
# newline, alignments with more than one shortest answer, and edited
# copies of a generated file.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-spot.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
k=0
pair() { k=$((k+1)); printf '%b' "$1" > "$H/a$k"; printf '%b' "$2" > "$H/b$k"; }
pair 'a\nb\nc\n'           'a\nb\nc\n'
pair 'a\nb\nc\n'           'a\nB\nc\n'
pair 'b\nc\n'              'a\nb\nc\n'
pair 'a\nb\n'              'a\nb\nc\nd\n'
pair 'a\nb\nc\n'           'b\nc\n'
pair 'a\nb\nc\n'           'a\nb\n'
pair '1\n2\n3\n4\n5\n6\n7\n8\n' '1\nX\n3\n4\n5\n6\nY\nZ\n8\n'
pair ''                    'a\nb\n'
pair 'a\nb\n'              ''
pair 'a\nb'                'a\nb\n'
pair 'a\nb\n'              'a\nc'
pair 'a\nb\na\n'           'b\n'
pair 'a\nb\nc\nd\n'        'a\nc\nb\nd\n'
pair 'x\nx\nx\ny\n'        'x\ny\nx\nx\n'
pair 'a\nb\nc\nd\ne\n'     'e\nd\nc\nb\na\n'
# generated: 60 lines, then random edits (seeded, so the same each run)
python3 - "$H" "$k" <<'PY'
import random, sys
h, k = sys.argv[1], int(sys.argv[2])
rnd = random.Random(426)
for t in range(40):
    base = [f"line {rnd.randrange(12)}" for _ in range(60)]
    new = list(base)
    for _ in range(8):
        op, at = rnd.randrange(3), rnd.randrange(len(new))
        if op == 0: del new[at]
        elif op == 1: new.insert(at, f"new {rnd.randrange(99)}")
        else: new[at] = f"changed {rnd.randrange(99)}"
    k += 1
    open(f"{h}/a{k}", "w").write("\n".join(base) + "\n")
    open(f"{h}/b{k}", "w").write("\n".join(new) + "\n")
PY
k=$((k+40))
for i in $(seq $k); do mcopy -i "$WORK/disk.img" "$H/a$i" "::/a$i"; mcopy -i "$WORK/disk.img" "$H/b$i" "::/b$i"; done
{ for i in $(seq $k); do printf 'cook spot.elf /a%d /b%d\nslurp "== %d $?"\n' $i $i $i; done
  printf 'cook spot.elf /nope /a1\nslurp "== missing $?"\n'; } > "$H/r.sh"
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
(cd "$H" && for i in $(seq $k); do diff a$i b$i; echo "== $i $?"; done; diff nope a1 2>/dev/null; echo "== missing $?") > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    spot is GNU diff ($k pairs, $(wc -l < "$WORK/want") lines: same, edits at each end, add, delete, hunks, empty, no last newline, ties, generated edits, a missing file 2)"
else echo "  FAIL  spot differs from diff:"; diff "$WORK/want" "$WORK/got" | head -40 | sed 's/^/        /'; fail=1; fi
exit $fail

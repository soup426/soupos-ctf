#!/usr/bin/env bash
# Build the player handout.
#
# The obvious handout leaks everything: `strings kernel.elf` prints all four
# flags, and src/challenge.c has them plus headchef's secret in plain source.
# So this produces a redacted tree instead.
#
# Placeholders are the SAME LENGTH as the real values, so the redacted tree
# compiles to a kernel with identical symbol addresses. That matters: stage 4
# needs the address of serve_the_special, and a player building locally has to
# get the same number the deployed image has. The script verifies that rather
# than assuming it.
#
#   ./scripts/mk-handout.sh          -> handout/soupos-handout.tar.gz
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=handout
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "== building the real challenge kernel (for the symbol map) =="
make clean >/dev/null 2>&1
make CHALLENGE=1 DOOM=0 >/dev/null 2>&1
cp kernel.elf "$WORK/real-kernel.elf"

echo "== staging a redacted source tree =="
rm -rf "$OUT"; mkdir -p "$OUT/soupos"
git ls-files -z | while IFS= read -r -d '' f; do
    case "$f" in
        .gitea/*)                    continue ;;   # CI config, and it names
                                                   # the secret it checks for
        docs/challenge-*.md)         continue ;;   # design + solutions: spoilers
        scripts/mk-handout.sh)       continue ;;   # organiser tool, and it
                                                   # contains the secret it
                                                   # redacts
        docs/next-steps.md)          continue ;;   # internal
        solve/*)                     continue ;;   # reference exploits
        handout/*)                   continue ;;
    esac
    mkdir -p "$OUT/soupos/$(dirname "$f")"
    cp "$f" "$OUT/soupos/$f"
done

python3 - "$OUT/soupos" <<'PY'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])

def same_len(real, label):
    """A placeholder of exactly the same length, so the layout is unchanged."""
    body = label
    inner = len(real) - len("cdctf{}")
    body = (label + "_" * inner)[:inner]
    return "cdctf{" + body + "}"

p = root / "src/challenge.c"
s = p.read_text()
for n in (1, 2, 3, 4):
    m = re.search(r'#define FLAG%d "(cdctf\{[^"]*\})"' % n, s)
    if not m:
        continue
    real = m.group(1)
    fake = same_len(real, "REDACTED_STAGE_%d" % n)
    assert len(fake) == len(real), (len(fake), len(real))
    s = s.replace('"%s"' % real, '"%s"' % fake)
p.write_text(s)

p = root / "src/users.c"
s = p.read_text()
s = s.replace('hash_secret("rosemary")', 'hash_secret("xxxxxxxx")')  # same length
p.write_text(s)

# The gate scripts log in as headchef, so they carry the secret too. Redact
# there as well: knowing it outright would hand over stage 2.
for rel in ("scripts/smoke-test.sh", "scripts/console-test.sh"):
    q = root / rel
    if q.exists():
        q.write_text(q.read_text().replace("rosemary", "xxxxxxxx"))

print("redacted flags and headchef's secret")
PY

echo "== verifying the redacted tree still builds to the same addresses =="
( cd "$OUT/soupos" && make clean >/dev/null 2>&1 && make CHALLENGE=1 DOOM=0 >/dev/null 2>&1 )

for sym in serve_the_special sc_state; do
    a=$(nm "$WORK/real-kernel.elf"        | awk -v s="$sym" '$3==s{print $1}')
    b=$(nm "$OUT/soupos/kernel.elf"       | awk -v s="$sym" '$3==s{print $1}')
    if [ "$a" = "$b" ]; then echo "  ok    $sym $a matches"
    else echo "  FAIL  $sym real=$a redacted=$b"; exit 1; fi
done

echo "== symbol map (addresses only, no string content) =="
nm "$WORK/real-kernel.elf" | sort > "$OUT/symbols.txt"
wc -l < "$OUT/symbols.txt" | xargs echo "  symbols:"

# Clean BEFORE checking, so the scan is not looking at build artifacts, and
# compare against the real flag values rather than the "cdctf{" prefix - the
# placeholders legitimately contain that prefix.
( cd "$OUT/soupos" && make clean >/dev/null 2>&1 && rm -f soupOS.iso disk.img iso/boot/kernel.elf )

echo "== leak check =="
fail=0
while IFS= read -r real; do
    [ -z "$real" ] && continue
    if grep -rqaF "$real" "$OUT" 2>/dev/null; then
        echo "  FAIL: real flag present: $real"
        grep -rlaF "$real" "$OUT" | sed 's/^/        /'
        fail=1
    fi
done < <(grep -ho 'cdctf{[^"]*}' src/challenge.c)

if grep -rqaF 'rosemary' "$OUT" 2>/dev/null; then
    echo "  FAIL: headchef's secret survived"
    grep -rlaF 'rosemary' "$OUT" | sed 's/^/        /'
    fail=1
fi
[ "$fail" = 0 ] || exit 1
echo "  ok    no real flags, no headchef secret"
tar czf "$OUT/soupos-handout.tar.gz" -C "$OUT" soupos symbols.txt
rm -rf "$OUT/soupos"
echo
echo "handout ready: $OUT/soupos-handout.tar.gz ($(du -h "$OUT/soupos-handout.tar.gz" | cut -f1))"

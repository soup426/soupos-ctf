#!/usr/bin/env bash
# bowls-test.sh - closed bowls and the sealed roster (split from cooks-test.sh
# so the two run side by side in check.sh; each section is its own boot).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bowls.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT

boot() {        # boot <disk> <log> <keys...>
    local disk=$1 log=$2; shift 2
    local mon="$WORK/mon.sock"; rm -f "$mon"
    qemu-system-i386 -accel kvm -cpu host -drive "file=$disk,format=raw,if=ide,index=0" \
        -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
        -serial "file:$log" -monitor "unix:$mon,server,nowait" >/dev/null 2>&1 &
    PID=$!; sleep 4
    python3 scripts/qemu_keys.py "$mon" "$@" >/dev/null 2>&1
    kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
}
soup32() { python3 - "$1" <<'PY'
import sys
M = 0xFFFFFFFF
def rotl(x, n): return ((x << n) | (x >> (32 - n))) & M
s = sys.argv[1].encode(); h = 0xB07B0C2D ^ ((len(s) * 0x9E3779B9) & M)
for b in s:
    h ^= b; h = rotl(h, 13); h = (h * 0x9E3779B9) & M; h ^= h >> 17
h ^= h >> 16; h = (h * 0x85EBCA6B) & M; h ^= h >> 13; h = (h * 0xC2B2AE35) & M; h ^= h >> 16
print(f"{h:08x}")
PY
}

# ── a closed bowl closes what is inside it (v0.53.1) ──
# Proven on v0.53.0: intern, refused at `cd /home/saucier`, read the file
# inside by its full path with pour and with spoon.elf, and listed the bowl
# with peek.elf.
cp disk.img "$WORK/bowl.img"
boot "$WORK/bowl.img" "$WORK/bowl.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "stir /home/saucier/RECIPE.TXT the secret sauce" "WAIT:1" "clockout" "WAIT:1" \
    "intern" "mint" "WAIT:2" "pour /home/saucier/RECIPE.TXT" "WAIT:1" "cook spoon.elf /home/saucier/RECIPE.TXT" "WAIT:2" \
    "cook peek.elf /home/saucier" "WAIT:2" "cook peek.elf /home/intern" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pour /home/saucier/RECIPE.TXT" "WAIT:1"
grep -q "Permission denied: /home/saucier/RECIPE.TXT" "$WORK/bowl.log" \
    && echo "  ok    the shell will not read a file inside another cook's home" || { echo "  FAIL  pour read through a closed bowl"; fail=1; }
grep -q "/spoon.elf: open /home/saucier/RECIPE.TXT for reading refused" "$WORK/bowl.log" \
    && echo "  ok    nor will SYS_OPEN" || { echo "  FAIL  spoon.elf read through a closed bowl"; fail=1; }
grep -q "/peek.elf: readdir /home/saucier refused" "$WORK/bowl.log" && ! grep -q "LS f 17 RECIPE.TXT" "$WORK/bowl.log" \
    && echo "  ok    and SYS_READDIR will not list it" || { echo "  FAIL  peek.elf listed a closed bowl"; fail=1; }
[ "$(grep -c "^the secret sauce" "$WORK/bowl.log")" = "1" ] && ! grep -q "readdir /home/intern refused" "$WORK/bowl.log" \
    && echo "  ok    the owner still reads it, and a cook still lists their own home" || { echo "  FAIL  the owner was locked out too"; fail=1; }
# ── the roster is the headchef's alone (v0.54.0) ──
# Proven on v0.53.4: an ordinary cook read every cook's PBKDF2 secret with
# pour and with spoon.elf. The CHALLENGE build keeps it readable on purpose
# (challenge-test.sh checks that).
cp disk.img "$WORK/roster.img"
boot "$WORK/roster.img" "$WORK/roster.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pour /etc/kitchen" "WAIT:1" "cook spoon.elf /etc/kitchen" "WAIT:2" \
    "secret" "WAIT:1" "basil" "WAIT:2" "thyme" "WAIT:1" "thyme" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "thyme" "WAIT:2" "whoami" "WAIT:1" "perms /etc/kitchen" "WAIT:1"
tr -d '\r' < "$WORK/roster.log" > "$WORK/roster.txt"
! grep -q '^headchef:0:\$p\$' "$WORK/roster.txt" && grep -q "Permission denied: /etc/kitchen" "$WORK/roster.txt" \
    && grep -q "/spoon.elf: open /etc/kitchen for reading refused" "$WORK/roster.txt" \
    && echo "  ok    a cook cannot read the roster, from the shell or a program" || { echo "  FAIL  a cook read the roster"; fail=1; }
grep -q "saucier has a new secret" "$WORK/roster.txt" && grep -qE "saucier  \(uid [0-9]+\)" "$WORK/roster.txt" \
    && echo "  ok    a cook who cannot read it still changes their secret and logs in with it" || { echo "  FAIL  secret or login broke"; fail=1; }
grep -q "owner: headchef   perms: rw----" "$WORK/roster.txt" \
    && echo "  ok    after the rewrite the roster is still the headchef's, rw----" || { echo "  FAIL  the roster's mode after a rewrite"; fail=1; }
exit $fail

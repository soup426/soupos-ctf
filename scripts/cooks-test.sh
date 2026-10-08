#!/usr/bin/env bash
# cooks-test.sh - homes, firing and closed bowls (split from users-test.sh in
# v0.53.1 so the two run side by side in check.sh; each section is its own boot).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-cooks.XXXXXX)
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

# ── home bowls (v0.52.2) ──
cp disk.img "$WORK/home.img"
boot "$WORK/home.img" "$WORK/home.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "pwd" "WAIT:1" "clockout" "WAIT:1" \
    "cook" "soup" "WAIT:2" "pwd" "WAIT:1" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pwd" "WAIT:1" "cd /home/cook" "WAIT:1" "cook hello.elf" "WAIT:2"
grep -q "made /home/saucier for saucier" "$WORK/home.log" && grep -q "made /home/cook for cook" "$WORK/home.log" \
    && echo "  ok    a hired cook gets a home at hire, an older one at their first login" || { echo "  FAIL  homes not made"; fail=1; }
grep -qx "  /home/saucier" <(tr -d '\r' < "$WORK/home.log") || grep -q "^/home/saucier\|  /home/saucier$" "$WORK/home.log"
[ "$(grep -cE '(^|  )/home/(saucier|cook)\s*$' "$WORK/home.log")" -ge 2 ] \
    && echo "  ok    each cook's shell starts in their home" || { echo "  FAIL  shells did not start at home"; grep -nE "pwd" -A1 "$WORK/home.log" | head -8; fail=1; }
grep -q "Permission denied: /home/cook" "$WORK/home.log" \
    && echo "  ok    a cook cannot enter another cook's home" || { echo "  FAIL  saucier entered /home/cook"; fail=1; }
grep -q "hello from ring 3" "$WORK/home.log" \
    && echo "  ok    programs are still found from a home (the root is searched too)" || { echo "  FAIL  hello.elf not found from a home"; fail=1; }
mdir -i "$WORK/home.img" ::/home 2>/dev/null | grep -qi "saucier" && [ "$(fsck.fat -n "$WORK/home.img" 2>&1 | wc -l)" -le 2 ] \
    && echo "  ok    mtools sees the homes and fsck is clean" || { echo "  FAIL  /home on disk or fsck"; fail=1; }
# ── fire hands a cook's files to the headchef (v0.53.0) ──
# uids are reused (the owner byte holds 0-55), so before this the next hire
# owned everything a fired cook left, closed home included.
cp disk.img "$WORK/fire.img"
boot "$WORK/fire.img" "$WORK/fire.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "stir /home/saucier/RECIPE.TXT the secret sauce" "WAIT:1" "cook hog.elf &" "WAIT:2" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "fire saucier" "WAIT:1" "kill %" "WAIT:2" "fire saucier" "WAIT:2" \
    "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2" "perms /home/saucier/RECIPE.TXT" "WAIT:1" \
    "hire saucier" "WAIT:1" "thyme" "WAIT:1" "thyme" "WAIT:2" "clockout" "WAIT:1" \
    "intern" "mint" "WAIT:2" "whoami" "WAIT:1" "cd /home/saucier" "WAIT:1"
grep -q "saucier is still at work (0 shells, 1 process)" "$WORK/fire.log" \
    && echo "  ok    fire refuses a cook with a process still running" || { echo "  FAIL  fire did not refuse a cook at work"; fail=1; }
grep -q "fired saucier (uid [0-9]*): 2 entries handed to the headchef" "$WORK/fire.log" \
    && echo "  ok    firing hands the cook's home and file to the headchef" || { echo "  FAIL  hand-over"; grep "fired" "$WORK/fire.log"; fail=1; }
grep -q "owner: headchef" "$WORK/fire.log" && grep -q "intern  (uid 2)" "$WORK/fire.log" && grep -q "Permission denied: /home/saucier" "$WORK/fire.log" \
    && echo "  ok    the next hire gets the uid but not the files" || { echo "  FAIL  the reused uid still reaches the old files"; fail=1; }
grep -q "but no home: /home/saucier is not theirs" "$WORK/fire.log" \
    && echo "  ok    a rehired name does not inherit the old home" || { echo "  FAIL  rehire took over the old home"; fail=1; }
[ "$(fsck.fat -n "$WORK/fire.img" 2>&1 | wc -l)" -le 2 ] \
    && echo "  ok    fsck is clean after the hand-over" || { echo "  FAIL  fsck after fire"; fsck.fat -n "$WORK/fire.img"; fail=1; }
# ── a closed bowl closes what is inside it (v0.53.1) ──
# Proven on v0.53.0: intern, refused at `cd /home/saucier`, read the file
# inside by its full path with pour and with cat.elf, and listed the bowl
# with ls.elf.
cp disk.img "$WORK/bowl.img"
boot "$WORK/bowl.img" "$WORK/bowl.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "stir /home/saucier/RECIPE.TXT the secret sauce" "WAIT:1" "clockout" "WAIT:1" \
    "intern" "mint" "WAIT:2" "pour /home/saucier/RECIPE.TXT" "WAIT:1" "cook cat.elf /home/saucier/RECIPE.TXT" "WAIT:2" \
    "cook ls.elf /home/saucier" "WAIT:2" "cook ls.elf /home/intern" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pour /home/saucier/RECIPE.TXT" "WAIT:1"
grep -q "Permission denied: /home/saucier/RECIPE.TXT" "$WORK/bowl.log" \
    && echo "  ok    the shell will not read a file inside another cook's home" || { echo "  FAIL  pour read through a closed bowl"; fail=1; }
grep -q "/cat.elf: open /home/saucier/RECIPE.TXT for reading refused" "$WORK/bowl.log" \
    && echo "  ok    nor will SYS_OPEN" || { echo "  FAIL  cat.elf read through a closed bowl"; fail=1; }
grep -q "/ls.elf: readdir /home/saucier refused" "$WORK/bowl.log" && ! grep -q "LS f 17 RECIPE.TXT" "$WORK/bowl.log" \
    && echo "  ok    and SYS_READDIR will not list it" || { echo "  FAIL  ls.elf listed a closed bowl"; fail=1; }
[ "$(grep -c "^the secret sauce" "$WORK/bowl.log")" = "1" ] && ! grep -q "readdir /home/intern refused" "$WORK/bowl.log" \
    && echo "  ok    the owner still reads it, and a cook still lists their own home" || { echo "  FAIL  the owner was locked out too"; fail=1; }
# ── the roster is the headchef's alone (v0.54.0) ──
# Proven on v0.53.4: an ordinary cook read every cook's PBKDF2 secret with
# pour and with cat.elf. The CHALLENGE build keeps it readable on purpose
# (challenge-test.sh checks that).
cp disk.img "$WORK/roster.img"
boot "$WORK/roster.img" "$WORK/roster.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "pour /etc/kitchen" "WAIT:1" "cook cat.elf /etc/kitchen" "WAIT:2" \
    "secret" "WAIT:1" "basil" "WAIT:2" "thyme" "WAIT:1" "thyme" "WAIT:2" "clockout" "WAIT:1" \
    "saucier" "thyme" "WAIT:2" "whoami" "WAIT:1" "perms /etc/kitchen" "WAIT:1"
tr -d '\r' < "$WORK/roster.log" > "$WORK/roster.txt"
! grep -q '^headchef:0:\$p\$' "$WORK/roster.txt" && grep -q "Permission denied: /etc/kitchen" "$WORK/roster.txt" \
    && grep -q "/cat.elf: open /etc/kitchen for reading refused" "$WORK/roster.txt" \
    && echo "  ok    a cook cannot read the roster, from the shell or a program" || { echo "  FAIL  a cook read the roster"; fail=1; }
grep -q "saucier has a new secret" "$WORK/roster.txt" && grep -qE "saucier  \(uid [0-9]+\)" "$WORK/roster.txt" \
    && echo "  ok    a cook who cannot read it still changes their secret and logs in with it" || { echo "  FAIL  secret or login broke"; fail=1; }
grep -q "owner: headchef   perms: rw----" "$WORK/roster.txt" \
    && echo "  ok    after the rewrite the roster is still the headchef's, rw----" || { echo "  FAIL  the roster's mode after a rewrite"; fail=1; }
exit $fail

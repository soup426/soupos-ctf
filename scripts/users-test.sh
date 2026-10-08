#!/usr/bin/env bash
# Are cooks' secrets stored the way they should be, and do old ones still work?
#
# The ordinary build keeps secrets as salted PBKDF2-HMAC-SHA256 (v0.46.0); the
# CHALLENGE build keeps the unsalted 32-bit AlphaSOUP hash, which is challenge
# stage 2. This checks the ordinary build, judging by what mtools reads back
# from /etc/kitchen:
#   - a fresh disk seeds only PBKDF2 entries, and a hired cook gets one too;
#   - a disk with an OLD-format kitchen still logs in, and that login (and
#     only that one) rewrites the entry as PBKDF2; the next boot logs in again;
#   - a wrong secret is refused in both formats.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-users.XXXXXX)
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

# ── a fresh disk ──
cp disk.img "$WORK/fresh.img"
boot "$WORK/fresh.img" "$WORK/fresh.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "hash rosemary" "WAIT:1" \
    "clockout" "WAIT:1" "saucier" "wrongsauce" "WAIT:2" "saucier" "basil" "WAIT:2" "whoami" "WAIT:1"
kitchen=$(mtype -i "$WORK/fresh.img" ::/etc/kitchen 2>/dev/null)
if [ "$(echo "$kitchen" | grep -c '^[a-z]*:[0-9]*:\$p\$20000\$[0-9a-f]\{32\}\$[0-9a-f]\{64\}$')" = "3" ] && \
   [ "$(echo "$kitchen" | wc -l)" = "3" ]; then
    echo "  ok    a fresh kitchen holds three salted PBKDF2 entries, the hired cook's included"
else
    echo "  FAIL  /etc/kitchen is not three PBKDF2 entries:"; echo "$kitchen" | sed 's/^/        /'; fail=1
fi
salts=$(echo "$kitchen" | cut -d'$' -f4 | sort -u | wc -l)
[ "$salts" = "3" ] && echo "  ok    every entry has its own salt" || { echo "  FAIL  salts repeat ($salts distinct)"; fail=1; }
grep -q "door stays locked" "$WORK/fresh.log" && echo "  ok    a wrong secret is refused" \
    || { echo "  FAIL  a wrong secret was not refused"; fail=1; }
grep -qE "saucier  \(uid [0-9]+\)" "$WORK/fresh.log" && echo "  ok    the hired cook logs in with the right secret" \
    || { echo "  FAIL  the hired cook could not log in"; fail=1; }
want=$(soup32 rosemary)
grep -qi "$want" "$WORK/fresh.log" && echo "  ok    the host's AlphaSOUP-32 matches soupOS's (0x$want)" \
    || { echo "  FAIL  the host's AlphaSOUP-32 port disagrees with soupOS's hash command"; fail=1; }

# ── an old-format kitchen ──
cp disk.img "$WORK/old.img"
printf 'headchef:0:%s\ncook:1:%s\n' "$(soup32 rosemary)" "$(soup32 soup)" > "$WORK/kitchen"
mmd -i "$WORK/old.img" ::/etc 2>/dev/null
mcopy -i "$WORK/old.img" "$WORK/kitchen" ::/etc/kitchen
boot "$WORK/old.img" "$WORK/old.log" "headchef" "letmein" "WAIT:2" "headchef" "rosemary" "WAIT:2" "whoami" "WAIT:1"
grep -q "door stays locked" "$WORK/old.log" && echo "  ok    an old-format entry still refuses a wrong secret" \
    || { echo "  FAIL  old-format wrong secret not refused"; fail=1; }
grep -q "headchef: secret upgraded to pbkdf2" "$WORK/old.log" && echo "  ok    an old-format entry logs in, and is upgraded at that login" \
    || { echo "  FAIL  old-format login or upgrade"; fail=1; }
kitchen=$(mtype -i "$WORK/old.img" ::/etc/kitchen 2>/dev/null)
echo "$kitchen" | grep '^headchef:0:\$p\$20000\$' >/dev/null && echo "$kitchen" | grep "^cook:1:$(soup32 soup)$" >/dev/null \
    && echo "  ok    only the cook who logged in was rewritten (cook still old until they do)" \
    || { echo "  FAIL  after upgrade the kitchen reads:"; echo "$kitchen" | sed 's/^/        /'; fail=1; }
boot "$WORK/old.img" "$WORK/old2.log" "headchef" "rosemary" "WAIT:2" "whoami" "WAIT:1"
grep -q "headchef: pbkdf2 check took" "$WORK/old2.log" && grep -qE "headchef  \(uid 0\)" "$WORK/old2.log" \
    && echo "  ok    the upgraded entry logs in on the next boot ($(grep -oE 'took [0-9]+ ticks' "$WORK/old2.log" | head -1))" \
    || { echo "  FAIL  the upgraded entry did not log in on the next boot"; fail=1; }
fsck.fat -n "$WORK/old.img" 2>&1 | wc -l | grep -x 2 >/dev/null && echo "  ok    fsck is clean after the kitchen was rewritten" \
    || { echo "  FAIL  fsck after the rewrite:"; fsck.fat -n "$WORK/old.img" 2>&1 | head -5; fail=1; }
# ── changing a secret (v0.52.1) ──
cp disk.img "$WORK/sec.img"
boot "$WORK/sec.img" "$WORK/sec.log" "headchef" "rosemary" "WAIT:1" \
    "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:1" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "secret" "WAIT:1" "wrongbasil" "WAIT:2" \
    "secret" "WAIT:1" "basil" "WAIT:2" "thyme" "WAIT:1" "thyme" "WAIT:1" "clockout" "WAIT:1" \
    "saucier" "basil" "WAIT:2" "saucier" "thyme" "WAIT:2" "whoami" "WAIT:1" "clockout" "WAIT:1" \
    "headchef" "rosemary" "WAIT:2" "secret saucier" "WAIT:1" "sage" "WAIT:1" "sage" "WAIT:1" "clockout" "WAIT:1" \
    "saucier" "sage" "WAIT:2" "whoami" "WAIT:1"
grep -q "secret change refused (wrong current secret)" "$WORK/sec.log" \
    && echo "  ok    changing your secret needs the current one" || { echo "  FAIL  a wrong current secret was accepted"; fail=1; }
[ "$(grep -c "saucier: secret changed" "$WORK/sec.log")" = "2" ] \
    && echo "  ok    a cook changed their own, and the headchef set it again" || { echo "  FAIL  secret changes"; fail=1; }
[ "$(grep -c "door stays locked" "$WORK/sec.log")" -ge 1 ] && [ "$(grep -cE "saucier  \(uid [0-9]+\)" "$WORK/sec.log")" = "2" ] \
    && echo "  ok    the old secret stops working and each new one logs in" || { echo "  FAIL  logins after the changes"; fail=1; }
mtype -i "$WORK/sec.img" ::/etc/kitchen 2>/dev/null | grep '^saucier:[0-9]*:\$p\$20000\$' >/dev/null \
    && echo "  ok    the changed secret is stored as PBKDF2" || { echo "  FAIL  saucier's stored secret"; fail=1; }
exit $fail

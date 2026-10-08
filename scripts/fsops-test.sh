#!/usr/bin/env bash
# Can programs remove files and make bowls, and only what they should?
#
# toss.elf and newbowl.elf call SYS_UNLINK and SYS_MKDIR. As the headchef: make a
# bowl and one inside it, write a file and remove it, and fail to remove a
# bowl or a file that is not there. Then as the ordinary cook: fail to remove
# a file the headchef owns, or make a bowl in the root (both what the shell
# itself would refuse); but remove the cook's own file, in a bowl the
# headchef has opened to all cooks. mtools and fsck judge
# the image afterwards.
set -uo pipefail
cd "$(dirname "$0")/.."
TESTDISK=$(mktemp /tmp/soupos-fsopsdisk.XXXXXX.img)
LOG=$(mktemp /tmp/soupos-fsops.XXXXXX.log)
MON=$(mktemp -u /tmp/soupos-fsops.XXXXXX.sock)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; rm -f "$MON" "$LOG" "$TESTDISK"; }
trap cleanup EXIT

cp disk.img "$TESTDISK"
qemu-system-i386 -accel kvm -cpu host -drive "file=$TESTDISK,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$LOG" -monitor "unix:$MON,server,nowait" >/dev/null 2>&1 &
PID=$!
sleep 4
python3 scripts/qemu_keys.py "$MON" "headchef" "rosemary" "WAIT:1" \
    "cook newbowl.elf /PANTRY /PANTRY/SHELF" "WAIT:2" "perms /PANTRY rwxrwx" "WAIT:1" \
    "stir /PRIVATE.TXT headchef eyes only" "WAIT:1" "perms /PRIVATE.TXT rw----" "WAIT:1" \
    "stir GONE.TXT this file will be removed" "WAIT:1" \
    "cook toss.elf /GONE.TXT /NOSUCH.TXT /PANTRY" "WAIT:2" \
    "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "cook toss.elf /HELLO.TXT" "WAIT:2" \
    "cook newbowl.elf /COOKBOWL" "WAIT:2" \
    "stir /PANTRY/MINE.TXT a cooks own file" "WAIT:1" "stir /PANTRY/KEPT.TXT stays" "WAIT:1" \
    "cook toss.elf /PANTRY/MINE.TXT" "WAIT:2" \
    "cook spoon.elf /PRIVATE.TXT" "WAIT:2" "cook spoon.elf /PANTRY/KEPT.TXT" "WAIT:2" >/dev/null 2>"$LOG.keys"
keys_rc=$?
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""

fail=0
# If the typing itself failed (an untypeable character, say), everything
# after that point silently never ran: say so instead of failing mysteriously.
if [ $keys_rc -ne 0 ]; then echo "  FAIL  could not type the whole sequence:"; tail -2 "$LOG.keys" | sed 's/^/        /'; fail=1; fi
rm -f "$LOG.keys"
ok()  { echo "  ok    $1"; }
bad() { echo "  FAIL  $1"; fail=1; }
grep -q "MKDIR made /PANTRY/SHELF" "$LOG" && mdir -i "$TESTDISK" ::/PANTRY 2>/dev/null | grep SHELF >/dev/null \
    && ok "newbowl.elf made a bowl inside a bowl, and mtools sees it" || bad "newbowl.elf"
grep -q "RM removed /GONE.TXT" "$LOG" && ! mdir -i "$TESTDISK" ::/GONE.TXT >/dev/null 2>&1 \
    && ok "toss.elf removed a file, and mtools agrees it is gone" || bad "toss.elf removing a file"
grep -q "RM failed /NOSUCH.TXT" "$LOG" && ok "removing a file that is not there fails" || bad "missing file"
grep -q "RM failed /PANTRY" "$LOG" && mdir -i "$TESTDISK" ::/PANTRY >/dev/null 2>&1 \
    && ok "toss.elf will not remove a bowl" || bad "toss.elf on a bowl"
grep -q "RM failed /HELLO.TXT" "$LOG" && mtype -i "$TESTDISK" ::/HELLO.TXT >/dev/null 2>&1 \
    && grep -q "unlink /HELLO.TXT refused" "$LOG" \
    && ok "an ordinary cook cannot remove the headchef's file" || bad "permission on unlink"
grep -q "RM removed /PANTRY/MINE.TXT" "$LOG" && mtype -i "$TESTDISK" ::/PANTRY/KEPT.TXT >/dev/null 2>&1 \
    && ok "but can remove their own, in a bowl open to all cooks" || bad "a cook removing their own file"
grep -q "MKDIR failed /COOKBOWL" "$LOG" && grep -q "mkdir /COOKBOWL refused" "$LOG" \
    && ! mdir -i "$TESTDISK" ::/COOKBOWL >/dev/null 2>&1 \
    && ok "a program cannot make a bowl where its cook may not write" || bad "permission on mkdir"
grep -q "open /PRIVATE.TXT for reading refused" "$LOG" && ! grep -q "headchef eyes only" <(sed -n '/cook spoon.elf \/PRIVATE.TXT/,$p' "$LOG") \
    && ok "a cook's program cannot read a file only the headchef may" || bad "permission on open"
grep -q "^stays" "$LOG" && ok "but reads one it may" || bad "a permitted open"
[ "$(fsck.fat -n "$TESTDISK" 2>&1 | wc -l)" -le 2 ] && ok "fsck is clean afterwards" \
    || { bad "fsck after the file operations:"; fsck.fat -n "$TESTDISK" 2>&1 | head -6; }
exit $fail

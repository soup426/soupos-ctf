#!/usr/bin/env bash
# tab-test.sh - Tab at the prompt finishes words (v0.60.10).
#
# Each line is typed in part, Tab is pressed, and the rest is typed; what
# the line became is read back from `leftovers` (the history listing) in the
# serial log, and what Tab listed is read from the lines it printed.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-tab.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
echo pepper > "$WORK/pepper.txt"; echo corn > "$WORK/peppercorn.txt"; echo red > "$WORK/paprika.txt"; echo nacl > "$WORK/SALT.TXT"
mmd -i "$WORK/disk.img" ::/tabs ::/tabs/pantry
for f in pepper.txt peppercorn.txt paprika.txt; do mcopy -i "$WORK/disk.img" "$WORK/$f" "::/tabs/$f"; done
mcopy -i "$WORK/disk.img" "$WORK/SALT.TXT" ::/tabs/pantry/SALT.TXT
T="KEY:tab"
keys=("headchef" "rosemary" "WAIT:1"
  "TYPE:slu" "$T" "a1"                                   # a builtin, then a space
  "TYPE:cook fli" "$T" "/HELLO.TXT"                     # a program, folded to lowercase
  "TYPE:cook spoon.elf /HELLO.TXT | lay" "$T" ""       # a program after a |
  "TYPE:slurp /tabs/pep" "$T" "$T" "TYPE:c" "$T" ""    # the shared part, a listing, then one fit
  "TYPE:slurp /tabs/pa" "$T" "$T" "TYPE:n" "$T" "$T" "" # a listing; a bowl gets a /, then into it
  "TYPE:slurp b2 ; slu" "$T" "c2"                      # a command after ;
  "TYPE:cook spoon.elf /HELLO.TXT >/tabs/pap" "$T" ""  # a path straight after >
  "TYPE:wh" "$T" "$T" "KEY:ctrl-c"                     # builtins listed
  "cd /tabs" "WAIT:1"
  "TYPE:cook sk" "$T" "/tabs/pepper.txt"               # programs from the root, paths from here
  "TYPE:slurp pap" "$T" ""
  "cd /" "WAIT:1"
  "TYPE:slurp /tabs/zz" "$T" "$T" "d4"                 # no fit: nothing happens
  "leftovers" "WAIT:2"
  "hire saucier" "WAIT:1" "basil" "WAIT:1" "basil" "WAIT:2"
  "hire intern" "WAIT:1" "mint" "WAIT:1" "mint" "WAIT:2"
  "cook spoon.elf /HELLO.TXT > /home/saucier/ROUX.TXT" "WAIT:2"
  "clockout" "WAIT:1" "intern" "mint" "WAIT:2"
  "TYPE:slurp /home/saucier/R" "$T" "$T" "e5"          # a home it may not read: nothing
  "TYPE:slurp /tabs/pap" "$T" "f6"                     # while a bowl it may read still works
  "leftovers" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" > "$WORK/out.log"
# What each line became, from the history listings.
grep -E '^ +[0-9]+  ' "$WORK/out.log" | sed -E 's/^ +[0-9]+  //' > "$WORK/hist.txt"
became() {   # became <what it should have become> <how it was typed>
    if grep -Fxq -- "$1" "$WORK/hist.txt"; then echo "  ok    $2  ->  $1"
    else echo "  FAIL  $2 did not become '$1'"; fail=1; fi
}
became "slurp a1"                                  "slu<Tab>a1"
became "cook flip.elf /HELLO.TXT"                  "cook fli<Tab>"
became "cook spoon.elf /HELLO.TXT | layer.elf "    "| lay<Tab>"
became "slurp /tabs/peppercorn.txt "               "/tabs/pep<Tab><Tab>c<Tab>"
became "slurp /tabs/pantry/SALT.TXT "              "/tabs/pa<Tab><Tab>n<Tab><Tab>"
became "slurp b2 ; slurp c2"                       "; slu<Tab>"
became "cook spoon.elf /HELLO.TXT >/tabs/paprika.txt " ">/tabs/pap<Tab>"
became "cook skim.elf /tabs/pepper.txt"            "cook sk<Tab> in /tabs"
became "slurp paprika.txt "                        "pap<Tab> in /tabs"
became "slurp /tabs/zzd4"                          "zz<Tab><Tab> (no fit)"
became "slurp /home/saucier/Re5"                   "R<Tab><Tab> in another cook's home"
became "slurp /tabs/paprika.txt f6"                "/tabs/pap<Tab> as that cook"
# What the second Tabs listed.
for want in "pepper.txt peppercorn.txt " "pantry/ paprika.txt " "whistle whoami "; do
    if grep -Fxq -- "$want" "$WORK/out.log"; then echo "  ok    a second Tab listed: $want"
    else echo "  FAIL  no listing '$want'"; fail=1; fi
done
if grep 'ROUX' "$WORK/out.log" | grep -v '> /home/saucier/ROUX.TXT' >/dev/null; then
    echo "  FAIL  Tab gave away a name in another cook's home"; fail=1
else echo "  ok    nothing from another cook's home was shown"; fi
exit $fail

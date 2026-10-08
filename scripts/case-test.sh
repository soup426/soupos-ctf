#!/usr/bin/env bash
# case-test.sh - case ... esac against bash (v0.60.19).
#
# Each line is typed into soupOS and run by bash with slurp as echo and
# `cook taste.elf` as test; every line prints markers, and the markers must
# come out the same, in the same order.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-case.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'x=apple ; case $x in a*) slurp R1=a-star ;; b*) slurp R1=b-star ;; esac'
  'case banana in a*|b*) slurp R2=ab ;; *) slurp R2=other ;; esac'
  'case cherry in a*) slurp R3=a ;; *) slurp R3=default ;; esac'
  'case zzz in a) slurp R4=a ;; esac ; slurp R4=status-$?'
  "case 'b c' in \"b c\") slurp R5=quoted-space ;; *) slurp R5=no ;; esac"
  'case abc in a?c) slurp R6=question ;; esac'
  "case x in \"*\") slurp R7=wrong ;; *) slurp R7=right ;; esac"
  'case a in a) case b in b) slurp R8=nested ;; esac ;; esac ; slurp R8=after'
  'case k in k) slurp R9a ; slurp R9b ;; j) slurp R9c ;; esac'
  'for f in one two three ; do case $f in t*) slurp R10=$f ;; esac ; done'
  'case p in (p) slurp R11=paren ;; esac'
  'case q in r) slurp R12=r ;; q) slurp R12=last ; esac'
  'case s in s) cook taste.elf 1 = 2 ;; esac ; slurp R13=$?'
  'p=ch* ; case cheese in $p) slurp R14=varpat ;; *) slurp R14=no ;; esac'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^R[0-9]+[a-z]?(=|$)' > "$WORK/got"
for l in "${LINES[@]}"; do
    b=${l//slurp/echo}; b=${b//cook taste.elf/test}
    bash -c "$b"
done | grep -E '^R[0-9]+[a-z]?(=|$)' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then
    echo "  ok    all ${#LINES[@]} case lines print what bash prints ($(wc -l < "$WORK/want") markers: globs, |, default, no match, quotes, ?, nesting, in a loop, (pat), no last ;;, status, a pattern from a variable)"
else echo "  FAIL  case differs from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

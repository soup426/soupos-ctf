#!/usr/bin/env bash
# quoteglob-test.sh - a quoted * ? [ is itself in patterns: ${NAME#pat},
# ${NAME/pat/rep} and case, against bash (v0.60.69).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-quoteglob.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
LINES=(
  'v="a*b?c[d]" ; slurp "Q1:${v#"a*"}:${v#a*}:${v%"[d]"}:${v%[d]}"'
  'slurp "Q2:${v/"?"/Q}:${v/?/Q}:${v//"*"/S}"'
  'slurp "Q3:${v#a\*}:${v%\]}"'
  'p="*" ; slurp "Q4:${v#$p}:${v#"$p"}:${v#a"$p"}"'
  'for w in "*" x ab "a*" ; do case $w in "*") slurp "Q5:$w star" ;; "a"*) slurp "Q5:$w a-something" ;; *) slurp "Q5:$w other" ;; esac ; done'
  'case "?" in "?") slurp Q6:one ;; esac ; case x in "?") slurp Q6:no ;; *) slurp Q6:x ;; esac'
  'case "a[b" in a"["b) slurp Q7:yes ;; *) slurp Q7:no ;; esac'
  'case abc in a?c) slurp Q8:glob ;; esac ; case abc in "a?c") slurp Q8:no ;; *) slurp Q8:literal-miss ;; esac'
)
keys=("headchef" "rosemary" "WAIT:1")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^Q[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do script+="${l//slurp/echo}"$'\n'; done
bash -c "$script" 2>/dev/null | grep -E '^Q[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    quoted pattern characters are themselves, as bash's ($(wc -l < "$WORK/want") lines: # % / //, \\* and \\], \$p bare and quoted, case \"*\" \"a\"* \"?\" a\"[\"b)"
else echo "  FAIL  quoted patterns differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

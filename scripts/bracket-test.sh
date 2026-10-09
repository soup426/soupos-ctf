#!/usr/bin/env bash
# bracket-test.sh - [abc], [a-z] and [!x] in globs and case patterns, against bash (v0.60.55).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-bracket.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
mkdir -p "$WORK/gl"
for f in a1.txt b2.txt c3.txt x.md; do echo "$f" > "$WORK/gl/$f"; done
mmd -i "$WORK/disk.img" ::/gl
for f in a1.txt b2.txt c3.txt x.md; do mcopy -i "$WORK/disk.img" "$WORK/gl/$f" "::/gl/$f"; done
LINES=(
  'cook call.elf G1: [ab]*'
  'cook call.elf G2: [a-b]?.txt'
  'cook call.elf G3: [!a]*'
  'cook call.elf G4: [^ab]*.txt'
  'cook call.elf G5: [z]*'
  'case b7 in [a-c][0-9]) slurp G6:yes ;; *) slurp G6:no ;; esac'
  'case x in [!a-m]) slurp G7:hi ;; esac'
  'case ] in []]) slurp G8:bracket ;; esac'
  'case [a in [a) slurp G9:literal ;; esac'
  'for f in [bc]* ; do slurp G10:$f ; done'
  'cook call.elf G11: "[ab]*"'
)
keys=("headchef" "rosemary" "WAIT:1" "cd /gl" "UNTIL:@soupOS:")
for l in "${LINES[@]}"; do keys+=("$l" "UNTIL:@soupOS:"); done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
tr -d '\r' < "$WORK/serial.log" | grep -E '^G[0-9]+:' > "$WORK/got"
script=""; for l in "${LINES[@]}"; do b=${l//cook call.elf/echo}; b=${b//slurp/echo}; script+="$b"$'\n'; done
( cd "$WORK/gl" && LC_ALL=C bash -c "$script" ) | grep -E '^G[0-9]+:' > "$WORK/want"
if cmp -s "$WORK/got" "$WORK/want"; then echo "  ok    all ${#LINES[@]} lines print what bash prints ($(wc -l < "$WORK/want") lines: sets, ranges, ! and ^, no match left as typed, case patterns, ] first, a [ with no ], for, quoted)"
else echo "  FAIL  brackets differ from bash:"; diff "$WORK/want" "$WORK/got" | sed 's/^/        /'; fail=1; fi
exit $fail

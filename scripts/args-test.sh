#!/usr/bin/env bash
# args-test.sh - a script's $1..$9, $# and $@, against sh (v0.60.2).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-args.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/P.TXT" <<'SCRIPT'
cook call.elf n=$# first=$1 second=$2 all=$@ > /P1.TXT
for a in $@ ; do cook call.elf [$a] >> /P2.TXT ; done
follow /Q.TXT inner ; cook call.elf back=$1 n=$# > /P3.TXT
SCRIPT
printf 'cook call.elf inner=$1 n=$# > /P4.TXT\n' > "$WORK/Q.TXT"
mcopy -i "$WORK/disk.img" "$WORK/P.TXT" ::/P.TXT; mcopy -i "$WORK/disk.img" "$WORK/Q.TXT" ::/Q.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "follow /P.TXT one \"two words\" three" "WAIT:6" "cook call.elf n=\$# > /P5.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; ( cd "$WORK/host" \
  && sed -e 's|cook call.elf|echo|g' -e 's| /P| P|g' -e 's|follow /Q.TXT|sh Q.sh|' "$WORK/P.TXT" > P.sh \
  && sed -e 's|cook call.elf|echo|g' -e 's| /P| P|g' "$WORK/Q.TXT" > Q.sh \
  && sh P.sh one "two words" three && echo n=0 > P5.TXT ) >/dev/null 2>&1
bad=0
for f in P1 P2 P3 P4 P5; do
    g=$(mcopy -n -i "$WORK/disk.img" "::/$f.TXT" - 2>/dev/null); w=$(cat "$WORK/host/$f.TXT" 2>/dev/null)
    [ "$g" = "$w" ] || { echo "        $f: [$(echo "$g" | tr '\n' '|')] sh [$(echo "$w" | tr '\n' '|')]"; bad=1; }
done
[ "$bad" = 0 ] && echo "  ok    a script's arguments are sh's: \$# \$1 \"two words\" \$@, for over \$@, a nested script's own \$1, the caller's given back, none outside a script" \
    || { echo "  FAIL  script arguments differ from sh"; fail=1; }
exit $fail

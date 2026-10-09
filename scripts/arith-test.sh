#!/usr/bin/env bash
# arith-test.sh - $(( )) against sh (v0.60.0).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-arith.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/A.TXT" <<'SCRIPT'
A=7 ; B=3
cook call.elf $((1+2)) $((A*B)) $(($A/B)) $((A%B)) $((-A+B)) $((2*(3+4))) $((A>B)) $((A<B)) $((A==7)) $((A!=7)) $((A<=7)) $((B>=4)) $((10-2-3)) $((100/7/2)) $((-7/2)) $((-7%2)) $((UNSET+1)) > /A1.TXT
N=0 ; N=$((N+1)) ; N=$((N+1)) ; cook call.elf $N > /A2.TXT
cook call.elf $((1/0)) > /A3.TXT ; cook call.elf $? > /A4.TXT
SCRIPT
mcopy -i "$WORK/disk.img" "$WORK/A.TXT" ::/A.TXT
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" "follow /A.TXT" "WAIT:5" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mkdir -p "$WORK/host"; ( cd "$WORK/host" && head -3 "$WORK/A.TXT" | sed -e 's|cook call.elf|echo|g' -e 's| /A| A|g' > a.sh && sh a.sh ) >/dev/null 2>&1
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
[ "$(val /A1.TXT)" = "$(cat "$WORK/host/A1.TXT")" ] && echo "  ok    17 expressions give sh's values: [$(val /A1.TXT)]" \
    || { echo "  FAIL  expressions: [$(val /A1.TXT)]  sh [$(cat "$WORK/host/A1.TXT")]"; fail=1; }
[ "$(val /A2.TXT)" = "$(cat "$WORK/host/A2.TXT")" ] && echo "  ok    a counter: N=\$((N+1)) twice gives $(val /A2.TXT)" \
    || { echo "  FAIL  the counter: [$(val /A2.TXT)]"; fail=1; }
# Division by zero: sh, non-interactive, would end the whole script here;
# soupOS skips the command with status 1 and carries on (written down).
! mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null | grep "A3.TXT" >/dev/null && [ "$(val /A4.TXT | tr -d '\n')" = "1" ] \
    && tr -d '\r' < "$WORK/serial.log" | grep "arithmetic: division by zero" >/dev/null \
    && echo "  ok    division by zero: said, the command not run, status 1" || { echo "  FAIL  division by zero"; fail=1; }
exit $fail

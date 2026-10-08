#!/usr/bin/env bash
# run-test.sh - run FILE, a script of command lines (v0.57.5).
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-run.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
cat > "$WORK/S.TXT" <<'SCRIPT'
# a comment, skipped
cook call.elf one > /R1.TXT
cook greet.elf && cook call.elf skipped > /R2.TXT
cook greet.elf || cook call.elf fallback > /R3.TXT
cook call.elf "two  words" ~ > /R4.TXT
cook call.elf $? > /R5.TXT
cook greet.elf
SCRIPT
printf 'cook call.elf mine > ~/M.TXT\nstir /etc/X.TXT nope\n' > "$WORK/T.TXT"
printf 'follow /SELF.TXT\n' > "$WORK/SELF.TXT"
for f in S T SELF; do mcopy -i "$WORK/disk.img" "$WORK/$f.TXT" "::/$f.TXT"; done
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "headchef" "rosemary" "WAIT:1" \
    "follow /S.TXT ; cook call.elf \$? > /R6.TXT" "WAIT:6" \
    "follow /SELF.TXT" "WAIT:3" "whoami" "WAIT:1" "clockout" "WAIT:1" "cook" "soup" "WAIT:2" \
    "follow /T.TXT ; cook call.elf \$? > ~/RS.TXT" "WAIT:3" \
    "follow /etc/kitchen ; cook call.elf \$? > ~/RK.TXT" "WAIT:2" >/dev/null 2>&1 \
    || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
# The host's sh, the same lines: greet.elf is (exit 42), and HOME is /.
mkdir -p "$WORK/host"; ( cd "$WORK/host" && sed -e 's|cook greet.elf|(exit 42)|g' -e 's|cook call.elf|echo|g' -e 's| /R| R|g' "$WORK/S.TXT" > s.sh \
  && HOME=/ sh s.sh; echo $? > R6.TXT ) >/dev/null 2>&1
got=$(mdir -b -i "$WORK/disk.img" ::/ 2>/dev/null | grep -oE "R[0-9]\.TXT" | sort | tr '\n' ' ')
want=$(cd "$WORK/host" && ls R*.TXT | sort | tr '\n' ' ')
bad=0
[ "$got" = "$want" ] || bad=1
for f in $want; do [ "$(mcopy -n -i "$WORK/disk.img" "::/$f" - 2>/dev/null)" = "$(cat "$WORK/host/$f")" ] || { echo "        $f: [$(mcopy -n -i "$WORK/disk.img" "::/$f" - 2>/dev/null)] sh [$(cat "$WORK/host/$f")]"; bad=1; }; done
[ "$bad" = 0 ] && echo "  ok    the script left what sh leaves: $want(comment skipped, && || quotes ~ \$?, status 42)" \
    || { echo "  FAIL  the script differs from sh: soupOS [$got] sh [$want]"; fail=1; }
val() { mcopy -n -i "$WORK/disk.img" "::$1" - 2>/dev/null; }
[ "$(val /home/cook/M.TXT)" = "mine" ] && ! mdir -b -i "$WORK/disk.img" ::/etc 2>/dev/null | grep X.TXT >/dev/null && [ "$(val /home/cook/RS.TXT)" = "1" ] \
    && echo "  ok    a cook's script runs as the cook: ~ is their home, /etc is closed, status 1" || { echo "  FAIL  the cook's script"; fail=1; }
[ "$(val /home/cook/RK.TXT)" = "1" ] && echo "  ok    a script the cook may not read is refused" || { echo "  FAIL  follow /etc/kitchen as a cook"; fail=1; }
L=$(tr -d '\r' < "$WORK/serial.log")
echo "$L" | grep "nested eight deep" >/dev/null && echo "$L" | grep "headchef  (uid 0)" >/dev/null \
    && echo "  ok    a script that runs itself stops eight deep, and the shell carries on" || { echo "  FAIL  recursion"; fail=1; }
exit $fail

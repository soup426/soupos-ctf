#!/usr/bin/env bash
# expiry-test.sh - expiry -d @SECONDS +FORMAT against GNU date -u -d @S
# (v0.60.131): every conversion, at 1970, the day before it, leap days in
# 2000 and 2024, the last second of 2024, 2100 (not a leap year), past
# 2^31, and others; the default format; and the RTC's %s against the
# host's clock.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-expiry.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
H="$WORK/h"; mkdir -p "$H"
F1='+%Y-%m-%d %H:%M:%S|%y %C %e %k %l %j %I %p|%a %A %b %h %B %u %w|%s %Z|%F %T %D %R|%%|%Q'
F2='+[%n|%t]'
: > "$H/r.sh"
for s in 0 -86400 951782400 1709164800 1735689599 4102444800 2147483648 1760000000 1000000000 43200; do
  printf "expiry -d @%s '%s'\nexpiry -d @%s '%s'\nexpiry -d @%s\n" "$s" "$F1" "$s" "$F2" "$s" >> "$H/r.sh"
done
mcopy -i "$WORK/disk.img" "$H/r.sh" ::/r.sh
keys=("headchef" "rosemary" "WAIT:1" 'follow /r.sh > /out.txt' "UNTIL:@soupOS:" "expiry +NOW:%s" "UNTIL:@soupOS:")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
mcopy -n -i "$WORK/disk.img" ::/out.txt "$WORK/got" 2>/dev/null || { echo "  FAIL  the recipe wrote nothing"; fail=1; exit 1; }
sed 's/^expiry -d /date -u -d /' "$H/r.sh" > "$H/r.bash"
LC_ALL=C bash "$H/r.bash" > "$WORK/want" 2>/dev/null
bad=0
cmp -s "$WORK/got" "$WORK/want" || { diff "$WORK/want" "$WORK/got" | head -20 | sed 's/^/        /'; bad=1; }
now=$(tr -d '\r' < "$WORK/serial.log" | grep -oE '^NOW:-?[0-9]+' | head -1 | cut -d: -f2)
host=$(date +%s)
[ -n "$now" ] && [ $((host - now)) -lt 120 ] && [ $((now - host)) -lt 120 ] || { echo "        the RTC says $now, the host $host"; bad=1; }
[ "$bad" = 0 ] && echo "  ok    expiry is GNU date -u ($(wc -l < "$WORK/want") lines: every conversion at 10 moments, 1969 to 2100, leap days, past 2^31; the default; the RTC within $((host - now)) s of the host)" \
    || { echo "  FAIL  expiry differs from date"; fail=1; }
exit $fail

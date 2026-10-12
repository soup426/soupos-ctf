#!/usr/bin/env bash
# winkeys-test.sh - SYS_WIN_KEYS (v0.60.190): the keys held, as a bitmap,
# only for the caller's own window while it has the focus and the desktop
# is not locked. prod.elf keys prints the held scancodes whenever they
# change. W is held a second (a raw sendkey with a hold time) four times:
#   1. prod keys' window on top: 11 (W), then none
#   2. Alt+Tab gives another window the focus: nothing
#   3. more Alt+Tabs bring prod keys back, the cloche comes down
#      ("idle 5"), W and the secret are typed at it: nothing
#   4. the cloche lifted, on top again: 11, then none
# and the limits first: a bad id, the victim's id, a pointer it never had.
# QEMU's PS/2 keyboard does not auto-repeat, so a held key is one make and
# one break.
set -uo pipefail
cd "$(dirname "$0")/.."
fail=0
WORK=$(mktemp -d /tmp/soupos-winkeys.XXXXXX)
PID=""
cleanup() { [ -n "$PID" ] && kill "$PID" 2>/dev/null; if [ "$fail" != 0 ] || [ "${KEEP:-0}" = 1 ]; then echo "  kept $WORK"; else rm -rf "$WORK"; fi; }
trap cleanup EXIT
cp disk.img "$WORK/disk.img"
printf 'idle 5\n' | mcopy -i "$WORK/disk.img" -o - ::countertop
L="MON:mouse_move -120 -120"
# Both prods start at the console, in the background, before the desktop,
# so what they print to stderr reaches the serial log (in a terminal on the
# desktop it would go to the terminal). Each waits for the desktop to open
# its window; whichever is first has id 0, so the other's is id ^ 1, and
# either way 400,140 is in prod keys' body (160x80 at 241,93 or 269,121)
# and not the victim's (100x100), so a click there gives it the focus.
# Four windows (Welcome, Pantry and the two), so three more Alt+Tabs bring
# it back after the first.
keys=("headchef" "rosemary" "WAIT:1" "cook prod.elf victim &" "WAIT:1" "cook prod.elf keys &" "WAIT:1" "countertop" "WAIT:4"
      "$L" "$L" "$L" "$L" "$L" "$L" "$L" "MON:mouse_move 400 140" "WAIT:1" "MON:mouse_button 1" "WAIT:1" "MON:mouse_button 0" "WAIT:1"
      "KEY:w 1000" "WAIT:2"
      "KEY:alt-tab" "WAIT:1" "KEY:w 1000" "WAIT:2"
      "KEY:alt-tab" "WAIT:1" "KEY:alt-tab" "WAIT:1" "KEY:alt-tab" "WAIT:1"
      "WAIT:6" "KEY:w 1000" "WAIT:2" "KEY:backspace" "WAIT:1" "rosemary" "WAIT:2"
      "KEY:w 1000" "WAIT:2" "TYPE:q" "WAIT:2")
qemu-system-i386 -accel kvm -cpu host -drive "file=$WORK/disk.img,format=raw,if=ide,index=0" \
    -cdrom soupOS.iso -boot d -m 128M -no-reboot -no-shutdown -display none \
    -serial "file:$WORK/serial.log" -monitor "unix:$WORK/mon.sock,server,nowait" >/dev/null 2>&1 &
PID=$!
python3 scripts/qemu_keys.py "$WORK/mon.sock" "${keys[@]}" >/dev/null 2>&1 || { echo "  FAIL  could not drive QEMU"; fail=1; }
kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; PID=""
python3 - "$WORK/serial.log" <<'PY' || fail=1
import re, sys
log = open(sys.argv[1], 'rb').read().decode('latin-1')
ok = True
def check(cond, what):
    global ok
    print(('  ok    ' if cond else '  FAIL  ') + what)
    ok &= bool(cond)
lines = re.findall(r'\[prod\] keys (.*)', log)
for want in ('bad-id -1', 'not-mine -1', 'bad-buf -1'):
    check(want in lines, 'limits: ' + want)
held = [l.split() for l in lines if not l.startswith(('bad-', 'not-'))]
# The keys the test sends while prod keys may have the focus: W (11), and
# the Alt (38) and Tab (0f) of the first Alt+Tab and the q (10) that ends
# it, which may or may not be caught by its 50 ms look. Anything else is a
# key nobody pressed, or one typed at the cloche: r o s e m a y, Backspace.
codes = {c for l in held for c in l if c != 'none'}
check(codes <= {'11', '38', '0f', '10'}, 'no key but the test\'s own: %r' % sorted(codes))
check('41' not in codes, 'no phantom F7 (0x41) from the mouse\'s config read')
w = [('11' in l) for l in held]
presses = sum(1 for i, x in enumerate(w) if x and (i == 0 or not w[i - 1]))
check(presses == 2, 'W seen held twice (phases 1 and 4), not in 2 or 3: %d' % presses)
tabs = re.findall(r'alt-tab to (.*)', log)
check(tabs and tabs[0].strip() != 'keys', 'the first Alt+Tab gave another window the focus: %r' % tabs[:1])
check(tabs and tabs[-1].strip() == 'keys', 'the last brought prod keys back: %r' % tabs)
check('cloche down (idle)' in log and 'cloche lifted' in log, 'the cloche came down and was lifted')
sys.exit(0 if ok else 1)
PY
[ "$fail" = 0 ] && echo "winkeys-test: PASS" || echo "winkeys-test: FAIL"
exit "$fail"

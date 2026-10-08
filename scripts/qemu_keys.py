import socket, sys, time

MON = sys.argv[1]
SCRIPT = sys.argv[2:]

KEYMAP = {' ':'spc', '\n':'ret', '.':'dot', '-':'minus', '_':'shift-minus',
          '/':'slash', ':':'shift-semicolon',
          '&':'shift-7', '%':'shift-5',
          '|':'shift-backslash', '<':'shift-comma', '>':'shift-dot',
          ',':'comma', '=':'equal', '+':'shift-equal', '*':'shift-8',
          # 8.3 aliases of long names contain '~' (ALONGR~1.TXT), so typing
          # one has to be possible.
          '~':'shift-grave_accent', '?':'shift-slash', '!':'shift-1'}

def keys_for(ch):
    if ch in KEYMAP: return [KEYMAP[ch]]
    if ch.isupper(): return ['shift-' + ch.lower()]
    if ch.isdigit(): return [ch]
    if ch.isalpha(): return [ch]
    raise ValueError('unmapped char %r' % ch)

s = socket.socket(socket.AF_UNIX)
for _ in range(50):
    try:
        s.connect(MON); break
    except OSError:
        time.sleep(0.2)
else:
    sys.exit('cannot connect to monitor')
time.sleep(0.5)
s.recv(65536)

def cmd(c):
    s.sendall((c + '\n').encode())
    time.sleep(0.05)
    try:
        s.recv(65536)
    except (BlockingIOError, socket.timeout):
        pass

# Type only once the kernel is listening (2026-10-08). Tests used to sleep a
# fixed 4-5 s after starting QEMU and then type; under load the boot could
# still be in the bootloader, which eats keys (console-test lost the 'h' of
# headchef that way, twice). The serial log says when the login is up, so
# wait for the banner there before the first key. A test that must type
# earlier sets QEMU_KEYS_NOWAIT=1; a serial port that is not a file is typed
# at as before.
#
# Where the log is: QEMU's `info chardev` does not say (it prints only
# "serial0: filename=file"), so ask the kernel which process is on the other
# end of the monitor socket and read its -serial argument.
import os, re, struct
def serial_log():
    try:
        pid = struct.unpack('3i', s.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))[0]
        args = open('/proc/%d/cmdline' % pid, 'rb').read().split(b'\0')
    except OSError:
        return None
    for i, a in enumerate(args[:-1]):
        if a == b'-serial' and args[i + 1].startswith(b'file:'):
            return args[i + 1][5:].decode()
    return None
if not os.environ.get('QEMU_KEYS_NOWAIT'):
    path = serial_log()
    if path:
        end = time.time() + 120
        while time.time() < end:
            try:
                if b'clock in to start your shift' in open(path, 'rb').read(): break
            except OSError: pass
            time.sleep(0.2)
        else:
            sys.exit('the login banner never appeared in ' + path)
        time.sleep(0.3)

for item in SCRIPT:
    if item.startswith('WAIT:'):
        time.sleep(float(item[5:]))
        continue
    # KEY:<qemu keyname> sends one raw sendkey, for keys the typing map has no
    # character for (KEY:ctrl-c, KEY:esc, ...).
    if item.startswith('KEY:'):
        cmd('sendkey ' + item[4:])
        continue
    # MON:<command> sends a raw monitor command (screendump, info, ...).
    # TYPE:<text> types the text WITHOUT a trailing Enter, for the editor and
    # for leaving a half-finished line at the prompt.
    if item.startswith('TYPE:'):
        for ch in item[5:]:
            for k in keys_for(ch):
                cmd('sendkey ' + k)
                time.sleep(0.03)
        time.sleep(0.2)
        continue
    if item.startswith('MON:'):
        cmd(item[4:])
        time.sleep(0.5)
        continue
    for ch in item:
        for k in keys_for(ch):
            cmd('sendkey ' + k)
            time.sleep(0.03)
    cmd('sendkey ret')
    time.sleep(0.4)

print('driver done')
s.close()

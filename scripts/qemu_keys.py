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
    except BlockingIOError:
        pass

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

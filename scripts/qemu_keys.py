import socket, sys, time

MON = sys.argv[1]
SCRIPT = sys.argv[2:]

KEYMAP = {' ':'spc', '\n':'ret', '.':'dot', '-':'minus', '_':'shift-minus',
          '/':'slash', ':':'shift-semicolon'}

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
    for ch in item:
        for k in keys_for(ch):
            cmd('sendkey ' + k)
            time.sleep(0.03)
    cmd('sendkey ret')
    time.sleep(0.4)

print('driver done')
s.close()

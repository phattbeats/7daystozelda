# kill only the game clients (daemons + their Chromes); keep relay/server/sink (team state lives in the relay)
import os, signal, sys
me = os.getpid(); pp = os.getppid(); which = sys.argv[1:] or ['A', 'B', 'C', 'D', 'E']
for p in os.listdir('/proc'):
    if not p.isdigit() or int(p) in (me, pp): continue
    try: c = open(f'/proc/{p}/cmdline', 'rb').read().split(b'\0')
    except Exception: continue
    for w in which:
        prof = ('/tmp/z4060/t/prof-' + w).encode()
        if (c[0] == b'python3' and len(c) > 5 and c[1] == b"/tmp/z4060/t/playd-gpu.py" and c[5] == prof) or \
           (c[0].endswith(b'chrome-headless-shell') and any(x == b'--user-data-dir=' + prof for x in c)):
            os.kill(int(p), signal.SIGKILL); print('kill', p, w)

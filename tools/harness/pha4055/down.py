import os,signal
me=os.getpid(); pp=os.getppid()
for p in os.listdir('/proc'):
    if not p.isdigit() or int(p) in (me,pp): continue
    try: c=open(f'/proc/{p}/cmdline','rb').read().split(b'\0')
    except: continue
    hit = (c[0]==b'python3' and len(c)>2 and c[1]==b'/tmp/z4055/t/playd-gpu.py') \
       or (c[0].endswith(b'chrome-headless-shell') and any(b'/tmp/z4055/t/prof-' in x for x in c)) \
       or (c[0]==b'./anchor' and os.readlink(f'/proc/{p}/cwd')=='/tmp/z4055/t/anchor')
    if not hit and c[0]==b'node' and len(c)>1 and c[1]==b'/tmp/z4055/web/server.js':
        hit = b'PORT=18471' in open(f'/proc/{p}/environ','rb').read().split(b'\0')
    if hit: os.kill(int(p),signal.SIGKILL); print('kill',p,c[0][-30:])

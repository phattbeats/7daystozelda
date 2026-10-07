import os,signal
me=os.getpid(); pp=os.getppid()
for p in os.listdir('/proc'):
    if not p.isdigit() or int(p) in (me,pp): continue
    try: c=open('/proc/%s/cmdline'%p,'rb').read().replace(b'\0',b' ').decode()
    except Exception: continue
    if ('playd-gpu.py' in c or 'chrome-headless-shell' in c or '/tmp/z4050/t/anchor/anchor' in c or '/tmp/z4050/repo/web/server.js' in c) and 'down.py' not in c:
        try: os.kill(int(p),signal.SIGKILL); print('killed',p,c[:60])
        except Exception as e: print(e)

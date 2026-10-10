import sys,time,json,threading
sys.path.insert(0,'/tmp/z4111/t'); import drv
fn=sys.argv[1]; HOSTC=sys.argv[2]; OTHER='A' if HOSTC=='B' else 'B'
ROOM=sys.argv[3]; GAP=float(sys.argv[4]) if len(sys.argv)>4 else 6
q="d=json.loads(ev('Module.ccall(\"%s\",\"string\",[\"number\"],[0])')); print(json.dumps({k:d.get(k) for k in ('present','auth','own','hp','phase','dying')}))"%fn
def probe(c):
    try: return json.loads(drv.run(c,q,20).strip().splitlines()[-1])
    except Exception as e: return {'err':str(e)[:60]}
print('before',probe(OTHER),probe(HOSTC))
t0=time.time()
drv.run(HOSTC,"page.goto('about:blank')",30)
first=None
while time.time()-t0<GAP+10:
    p=probe(OTHER); dt=round(time.time()-t0,1)
    print(dt,p)
    if first is None and p.get('auth')==p.get('own'): first=dt
    if first is not None and dt>first+4: break
print('takeover_at',first)
print('rejoining',HOSTC)
code="NAME='%s'; ROOM='%s'\n"%(HOSTC,ROOM)+open('/tmp/z4111/t/join.py').read()
drv.run(HOSTC,code,300)
print('rejoined',probe(OTHER),probe(HOSTC))

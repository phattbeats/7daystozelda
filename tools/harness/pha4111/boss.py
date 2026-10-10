# usage: boss.py LABEL WARP HOSTFIRST(A|B) ; runs stale-stream + clean-leave/rejoin on whichever client is host
import sys,time,json,os
sys.path.insert(0,'/tmp/z4111/t'); import drv
LABEL,WARP=sys.argv[1],sys.argv[2]
ROOM=os.environ.get('ROOM','ar3'); ALLOW_PRE=os.environ.get('PRE','')
def E(c,t=30):
    try: return json.loads(drv.run(c,"print(json.dumps(json.loads(ev('Module.ccall(\"anchor_test_enemies\",\"string\",[],[])'))))",t).strip().splitlines()[-1])
    except Exception as e: return {'err':str(e)[-80:]}
def S(d): 
    if 'err' in d: return d
    return {'auth':d['auth'],'self':d['self'],'n':len(d['enemies']),'hp':[e['hp'] for e in d['enemies']][:4],'dying':[e['dying'] for e in d['enemies']][:4]}
def loglines(c):
    return open('/tmp/z4111/t/playd-%s.log'%c,errors='ignore').read()
off={c:len(loglines(c)) for c in 'AB'}
for c in 'AB':
    drv.run(c,ALLOW_PRE+"raid('warp:%s'); time.sleep(16)"%WARP,90)
    time.sleep(2)
time.sleep(10)
sa,sb=E('A'),E('B'); print(LABEL,'entered','A',S(sa),'B',S(sb))
hostid=sa.get('auth'); HOST='A' if sa.get('self')==hostid else 'B'; OTHER='B' if HOST=='A' else 'A'
print('host',HOST)
HIT=os.environ.get('HIT')
if HIT:
    drv.run(HOST,open('/tmp/z4111/t/hit2.py').read().replace("HITARG",repr(HIT)),60); time.sleep(4)
    print(LABEL,'after hit','A',S(E('A')),'B',S(E('B')))
# stale stream
import threading
t=threading.Thread(target=lambda:drv.run(HOST,"ev('(()=>{var t=Date.now();while(Date.now()-t<8000)0;})()')",60)); t0=time.time(); t.start()
snaps=[]
while t.is_alive():
    d=E(OTHER,15); snaps.append((round(time.time()-t0,1),S(d),[e['pos'] for e in d.get('enemies',[])][:1])); time.sleep(1.5)
t.join(); time.sleep(3)
print(LABEL,'STALE other-auth-changes',sorted(set(s[1].get('auth') for s in snaps)),'moved',len(set(str(s[2]) for s in snaps))>2,'n',set(s[1].get('n') for s in snaps))
print(LABEL,'after unfreeze','A',S(E('A')),'B',S(E('B')))
# clean leave
t0=time.time(); drv.run(HOST,"page.goto('about:blank')",30)
first=None; n=[]
for i in range(8):
    d=E(OTHER,15); s=S(d); n.append(s.get('n'))
    if first is None and s.get('auth')==s.get('self'): first=round(time.time()-t0,1)
    time.sleep(0.5)
print(LABEL,'LEAVE other took auth at',first,'enemy n',n)
code="NAME='%s'; ROOM='%s'\n"%(HOST,ROOM)+open('/tmp/z4111/t/join.py').read()
drv.run(HOST,code,300)
drv.run(HOST,ALLOW_PRE+"raid('warp:%s'); time.sleep(16)"%WARP,90); time.sleep(10)
sa,sb=E('A'),E('B'); print(LABEL,'REJOIN','A',S(sa),'B',S(sb))
for c in 'AB':
    new=loglines(c)[off[c]:]
    bad=[l for l in new.splitlines() if any(k in l for k in ('Fuzzy','No match','split brain','parse error','dropped malformed','Uncaught','abort','null function'))]
    print(LABEL,'canary',c,len(bad),bad[:3])

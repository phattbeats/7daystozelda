import sys,time,json,threading
sys.path.insert(0,'/tmp/z4111/t')
import drv
PRO=sys.argv[1] if len(sys.argv)>1 else 'gm'   # probe name
HOSTC=sys.argv[2] if len(sys.argv)>2 else 'B'
OTHER='A' if HOSTC=='B' else 'B'
FRZ=int(sys.argv[3]) if len(sys.argv)>3 else 8
PROBES={'gm':'anchor_test_gm','mo':'anchor_test_mo','sst':'anchor_test_sst','kd':'anchor_test_kd','tw':'anchor_test_tw','gdf':'anchor_test_gdf','gn2':'anchor_test_gn2','gnd':'anchor_test_gnd','va':'anchor_test_va','vf':'anchor_test_vf'}
fn=PROBES[PRO]
q="d=json.loads(ev('Module.ccall(\"%s\",\"string\",[\"number\"],[0])')); print(json.dumps({k:d.get(k) for k in ('present','auth','own','hp','phase','pos','tick','dying')}))"%fn
def probe(c):
    try: return json.loads(drv.run(c,q,20).strip().splitlines()[-1])
    except Exception as e: return {'err':str(e)[:60]}
print('before', 'A',probe('A'),'B',probe('B'))
def fz(): drv.run(HOSTC,"ev('(()=>{var t=Date.now();while(Date.now()-t<%d)0;})()')"%(FRZ*1000),60)
t=threading.Thread(target=fz); t0=time.time(); t.start()
first=None
while t.is_alive():
    p=probe(OTHER); dt=round(time.time()-t0,1)
    print(dt,'other',p.get('auth'),p.get('own'),p.get('tick'),p.get('pos'))
    if first is None and p.get('auth')==p.get('own'): first=dt
    time.sleep(0.7)
t.join(); time.sleep(4)
print('takeover_at',first)
print('after A',probe('A'),'B',probe('B'))

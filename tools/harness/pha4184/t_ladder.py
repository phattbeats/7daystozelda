# PHA-4184: A climbs the Kakariko house ladder (x=1196, y 80..410, faces +x); B samples the puppet every frame.
import sys,json,time,statistics
sys.path.insert(0,'/tmp/z4181/t')
from drv import run, both
X=float(sys.argv[1]) if len(sys.argv)>1 else 1180; Z=-16; YAW=0x4000
if '--warp' in sys.argv:
    print(both("kit(); raid('warp:db'); time.sleep(16)\nprint(json.dumps({k:R().get(k) for k in ('scene','room','link')}))"))
A="""
y=FL(%f,%d,600); W(%f,y,%d,%d); time.sleep(1.5)
hold('w',7.0); time.sleep(0.5)
print(json.dumps({'start':y,'link':R().get('link')}))
"""%(X,Z,X,Z,YAW)
B="""
time.sleep(1.0)
ev('window.__cl=[]; window.__t0=performance.now(); window.__iv=setInterval(()=>{const r=JSON.parse(Module.ccall("anchor_test_climb","string",[],[])); window.__cl.push([(performance.now()-window.__t0)/1000, r]);},4)')
time.sleep(8.5)
print(json.dumps(ev('clearInterval(window.__iv); window.__cl')))
"""
a,b=both(A,B)
print('A:',a.strip().splitlines()[-1])
S=json.loads(b.strip().splitlines()[-1])
rows=[(t,s[0]) for t,s in S if s]
clim=[(t,e) for t,e in rows if e['climbing']]
print('samples',len(rows),'climbing',len(clim),'smooth',sum(e['smooth'] for t,e in clim))
def stats(key):
    ys=[e[key][1] for t,e in clim if key in e]
    d=[b-a for a,b in zip(ys,ys[1:])]
    nz=[abs(x) for x in d if abs(x)>0.01]
    return dict(n=len(ys),span=round(ys[-1]-ys[0],1) if ys else 0,still=sum(1 for x in d if abs(x)<=0.01),moving=len(nz),maxstep=round(max(nz),1) if nz else 0,meanstep=round(statistics.mean(nz),2) if nz else 0)
print('raw  ',stats('raw')); print('shown',stats('shown'))
json.dump(S,open('/tmp/z4184/ladder_samples.json','w'))

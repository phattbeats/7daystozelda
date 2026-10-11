# PHA-4196 driver helpers (run from the host shell)
import sys, json, time; sys.path.insert(0,'/tmp/z4196/t')
from drv import run, both
H="exec(open('/tmp/z4196/t/va.py').read())\nexec(open('/tmp/z4196/t/tw.py').read().split('def kit')[0].replace(\"exec(open('/tmp/z4196/t/h2.py').read())\",''))\n"
def st(c):
    code=H+"""
t=TW(); r=R(); d=VA()
print(json.dumps({'scene':t.get('scene'),'room':t.get('room'),'link':[round(v) for v in (t.get('link') or [0,0,0])[:3]],'hp':r.get('health'),'go':r.get('gameOver'),
 'va':{k:d.get(k) for k in ('present','cs','fp','phase','clear','door','pos')} if d.get('present') else None}))"""
    out=run(c,code,60).strip().splitlines()
    try: return json.loads(out[-1])
    except Exception: return out
def pr(tag):
    for c in 'AB': print(tag,c,json.dumps(st(c)),flush=True)
def shot(c,name): run(c,"shot('/tmp/z4196/ev/%s_%s.png')"%(name,c),60)
def do(c,code,t=120): return run(c,H+code,t)
# finds gSaveContext.health (cap,health pair, confirmed by a heal) then zeroes it
KILL_OLD="""ev('if(window._hg){clearInterval(window._hg);window._hg=null}'); cv('gCheats.InfiniteHealth',0)
raid('heal'); time.sleep(0.3); r=R()
c=ev('(()=>{const h=HEAP16,o=[];for(let i=0;i<h.length-1;i++){if(h[i]==%d&&h[i+1]==%d)o.push(i+1)}return o})()'%(r['healthCapacity'],r['health']))
cv('gCheats.InfiniteHealth',0); ev('HEAP16[%d]=8'%c[-1]) if False else None
for a in c:
    v=ev('HEAP16[%d]'%a); ev('HEAP16[%d]=%d'%(a,v-8)); time.sleep(0.2)
    if R()['health']==v-8: ev('HEAP16[%d]=0'%a); print('killaddr',a*2); break
    ev('HEAP16[%d]=%d'%(a,v))
"""
def logs(c, pat='BossEntry|LifeSync|CoopWarp|Scene Init', n=15):
    import re,subprocess
    L=[l.rstrip() for l in open('/tmp/z4196/t/playd-%s.log'%c) if '[con]' in l and re.search(pat,l)]
    for l in L[-n:]: print(c, l[6:])
HP_ADDR=8706648  # gSaveContext.health byte address in the r2 (5d6c33b) wasm; 8706600 in b2558c7
KILL="ev('if(window._hg){clearInterval(window._hg);window._hg=null}'); cv('gCheats.InfiniteHealth',0); ev('HEAP16[%d]=0')"%(HP_ADDR>>1)

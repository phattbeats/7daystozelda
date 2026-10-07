if int(globals().get('CAP',256)): cv('gSevenDays.BaseCap', int(globals().get('CAP',256)))
exec(open('/tmp/z4062/t/fortn.py').read())
from collections import Counter
N=int(globals().get('N',256)); KIND=globals().get('KIND','mixed')
cand = candidates(KIND)
for k in set(k for k,*_ in cand): KIT(k, 400)
W(150, 0, 2150, 0x8000); time.sleep(0.5)
placed=0; tried=0; cnt=Counter()
def nplaced(): return len([p for p in B()['placeables'] if p['scene'] == 81])
for k, x, y, z, r in cand:
    if placed>=N: break
    PL(T[k], x, y, z, r); tried+=1; time.sleep(0.06)
    if tried%10==0: placed=nplaced()
time.sleep(2); placed=nplaced()
b=B(); mine=[p for p in b['placeables'] if p['scene']==81]
print('target',N,'placed',placed,'tried',tried,'actors',b.get('placeableActors'),'dyna',b.get('dyna'),'refusals',b.get('refusals'),dict(Counter(p['type'] for p in mine)))

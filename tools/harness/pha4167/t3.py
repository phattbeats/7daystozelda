from prop import *
from collections import Counter
def cnt(c):
    a=W(c,-1)['actors']; k=Counter(x['id'] for x in a); return {n:k.get(i,0) for n,i in list(IDS.items())+[('item00',21)]}
for name,id_ in IDS.items():
    b0=(cnt('A'),cnt('B'))
    W('A',4,id_); time.sleep(3)
    a1,b1=cnt('A'),cnt('B')
    print(name,'A',b0[0][name],'->',a1[name],' B',b0[1][name],'->',b1[name],' item00 A',b0[0]['item00'],'->',a1['item00'],' B',b0[1]['item00'],'->',b1['item00'])

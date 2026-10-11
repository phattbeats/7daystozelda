from prop import *
ct=lambda c:[sum(1 for a in W(c,-1)['actors'] if a['id']==273), sum(1 for a in W(c,-1)['actors'] if a['id']==21)]
print('before',ct('A'),ct('B'))
W('A',4,273); time.sleep(3)
print('hit A ',ct('A'),ct('B'))
# reverse direction: B breaks a bush, A should follow
for c in 'AB': W(c,0,293,0,60)
time.sleep(1.5)
k=lambda c:sum(1 for a in W(c,-1)['actors'] if a['id']==293)
print('bush before',k('A'),k('B')); W('B',4,293); time.sleep(3); print('bush after B hit',k('A'),k('B'))

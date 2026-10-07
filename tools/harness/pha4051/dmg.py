exec(open('/tmp/z4051/t/mo.py').read())
ev('''(()=>{ if(window._hg) clearInterval(window._hg); })()''')
cv('gCheats.InfiniteHealth',0)
h0=MO()['health']; print('health start',h0)
t0=time.time()
while time.time()-t0<150:
    d=MO(); t=d['tents'][0]
    if d.get('held') and t.get('act') in (4,5): break
    time.sleep(0.25)
shot('/tmp/z4051/t/ev/dmg-B-held.png')
time.sleep(4)
d=MO(); print('held',d.get('held'),'health',d['health'])
shot('/tmp/z4051/t/ev/dmg-B-hurt.png')
cv('gCheats.InfiniteHealth',1)

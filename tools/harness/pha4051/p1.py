exec(open('/tmp/z4051/t/mo.py').read())
t0=time.time(); hs=[]
while time.time()-t0<80:
    d=MO(); t=d['tents'][0]
    if d.get('held') and t.get('act') in (4,5): break
    time.sleep(0.25)
print('B held at', round(time.time()-t0,1), 'tent act', t.get('act'), 'victim', t.get('victim'))
shot('/tmp/z4051/t/ev/p1-B-held.png')
for k in range(35):
    page.keyboard.down('x'); time.sleep(0.03); page.keyboard.up('x'); time.sleep(0.03)
    if k==10: hs.append(MO()['health'])
shot('/tmp/z4051/t/ev/p1-B-mid.png')
for k in range(40):
    page.keyboard.down('x'); time.sleep(0.03); page.keyboard.up('x'); time.sleep(0.03)
for i in range(6):
    d=MO(); t=d['tents'][0]
    print(i,'tent',t.get('act'),'victim',t.get('victim'),'tentHeld',t.get('held'),'Bheld',d.get('held'),'link',[round(v) for v in d.get('link')],'health',d['health'])
    time.sleep(0.4)
shot('/tmp/z4051/t/ev/p1-B-after.png')
print('health during hold', hs)

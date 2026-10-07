exec(open('/tmp/z4051/t/mo.py').read())
t0=time.time(); done=False
while time.time()-t0<WAIT:
    d=MO(); t=d['tents'][0]
    if d.get('act')==10 and not d.get('sup',False) is None: pass
    if d.get('act')==10 and t.get('act')!=100:
        shot('/tmp/z4051/t/ev/p2-%s-attack.png'%TAG)
        if HIT: MO(6)
        print(TAG,'core ATTACK seen at',round(time.time()-t0,1),'tent act',t['act']); done=True
        for i in range(6):
            time.sleep(0.4); d=MO(); t=d['tents'][0]
            print(TAG,i,'core',d['act'],'tent',t['act'],'cut',t['cut'])
        shot('/tmp/z4051/t/ev/p2-%s-after.png'%TAG)
        break
    time.sleep(0.2)
if not done: print(TAG,'no core attack in window')

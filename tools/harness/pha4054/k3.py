exec(open('/tmp/z4054/t/gd.py').read())
W(250.0,0.0,250.0,0x6000)
t0=time.time(); last=None
if NAME=='A': GD(2,3); time.sleep(2); GD(7,0); print('stun sent',flush=True)
while time.time()-t0<120:
    g=GD(); s=(g['phase'],g['sup'],g['act'],g['hp'],g['dying'],g['scene'],g['pending'])
    if s!=last: print(round(time.time()-t0,1),s,flush=True); last=s
    t=time.time()-t0
    if NAME=='B':
        if 4<t<5 and not globals().get('la'): GD(1,0x2000); la=1; print('light arrow',flush=True)
        if t>8 and g['hp']>0: GD(1,0)
    if g['phase']==2 or g['scene']!=25: break
    time.sleep(0.25)
shot('/tmp/z4054/t/ev/k3-%s.png'%NAME)

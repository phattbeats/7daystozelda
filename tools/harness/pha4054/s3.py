exec(open('/tmp/z4054/t/gd.py').read())
W(250.0,0.0,250.0,0x6000)
hit=False; t0=time.time(); log=[]
while time.time()-t0<90:
    g=GD()
    if g['ball']:
        log.append((round(time.time()-t0,1),g.get('ballMode'),[round(v) for v in g['ballPos']],g['act'],g.get('t0')))
        if NAME=='B' and g.get('ballSup') and not hit and g['ballPos'][1]<150:
            GD(5,0); hit=True; log.append('HIT')
    time.sleep(0.12)
    if hit and len([l for l in log if l!='HIT'])>40 and g['ball']==False: break
for l in log: print(l)

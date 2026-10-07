exec(open('/tmp/z4054/t/gd.py').read())
W(100.0,1086.0,-190.0,0x4000); time.sleep(3)
t0=time.time(); ph=None
while time.time()-t0<300:
    key('x',0.1); time.sleep(1.2)
    g=G2()
    if (g.get('phase'),g.get('cs'))!=ph:
        ph=(g.get('phase'),g.get('cs')); print(int(time.time()-t0),'phase',ph,flush=True)
    if g.get('phase')==1: break
print(json.dumps(G2())); shot('/tmp/z4054/t/ev/t6-%s.png'%NAME)

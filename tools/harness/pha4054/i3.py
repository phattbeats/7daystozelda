exec(open('/tmp/z4054/t/gd.py').read())
t0=time.time(); last=None
while time.time()-t0<300:
    key('x',0.1); time.sleep(0.8)
    g=GD(); st=(g.get('phase'),g.get('cs'),g.get('act'))
    if st!=last: print(int(time.time()-t0),st,flush=True); last=st
    if g.get('phase')==1: break
print(json.dumps(GD())); shot('/tmp/z4054/t/ev/i3-%s.png'%NAME)

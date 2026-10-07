exec(open('/tmp/z4051/t/mo.py').read())
kit()
t0=time.time(); n=0
while time.time()-t0<150:
    key('x',0.08)
    d=MO()
    if n%6==0: shot('/tmp/z4051/t/i-%s-%02d.png'%(TAG,n//6))
    n+=1
    if d.get('cs')==0 and d.get('phase')==1: print('fight',int(time.time()-t0)); break
    time.sleep(1.2)
d=MO(); print(json.dumps(d)); shot('/tmp/z4051/t/fight-%s.png'%TAG)

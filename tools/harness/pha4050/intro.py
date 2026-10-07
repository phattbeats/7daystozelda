exec(open('/tmp/z4050/t/vf.py').read())
t0=time.time(); n=0; last=None
while time.time()-t0<110:
    d=S(); 
    if n%4==0: shot('/tmp/z4050/t/s02-%s-intro%02d.png'%(TAG,n))
    n+=1
    key_t=d.get('intro')
    if d.get('present',True) and d.get('intro')==0: print('fight',int(time.time()-t0),d); break
    if d.get('intro') in (None,1) : hold('w',1.0)
    key('x',0.08)
    if d.get('intro')!=last: print(int(time.time()-t0),'intro',d.get('intro'),d.get('link')); last=d.get('intro')
    time.sleep(0.5)
print(S())
shot('/tmp/z4050/t/s03-%s-fight.png'%TAG)

exec(open('/tmp/z4051/t/mo.py').read())
t0=time.time(); n=0; last=None
while time.time()-t0<DUR:
    d=MO()
    s=(d['phase'],d['cs'],d['dying'],d['hearts'],d['warps'],d['clear'],d['hp'],len(d.get('tents',[])))
    if s!=last: print(round(time.time()-t0,1),s,d['pos'][:3]); last=s
    if n%8==0: shot('/tmp/z4051/t/d-%s-%02d.png'%(TAG,n//8))
    n+=1
    time.sleep(0.5)
print('final',json.dumps({k:d[k] for k in ('phase','cs','dying','hearts','warps','clear','hp','scene','link','health')}))
shot('/tmp/z4051/t/d-%s-end.png'%TAG)

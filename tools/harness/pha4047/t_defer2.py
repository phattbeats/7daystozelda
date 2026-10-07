exec(open('/tmp/z4047/t/drv.py').read())
run('A',"W(-890,-1300,-2804,0x8000)\n")
t0=time.time()
while time.time()-t0<60:
    a=kd('A')
    if a['bc']==0 and a['phase']==1: break
    time.sleep(1)
print('A fight after', round(time.time()-t0), a['act'])
time.sleep(2)
b=kd('B'); print('B before', {k:b[k] for k in ('act','phase','cs','bc','sup','dying','pending','link')})
kd('A',4,0); time.sleep(2)
a=kd('A'); b=kd('B')
print('A after kill', {k:a[k] for k in ('act','phase','cs','hp')})
print('B after kill', {k:b[k] for k in ('act','phase','cs','bc','sup','dying','pending','hp')})
# B now drops in: its intro plays, then the deferred defeat
run('B',"W(-890,-1300,-2804,0x8000)\n")
t0=time.time(); last=None; n=0
while time.time()-t0<90:
    b=kd('B'); st=(b['act'],b['cs'],b['bc'],b['pending'],b['clear'])
    if st!=last: print(round(time.time()-t0,1),'B',st,'hp',b['hp'],'csAction',b['csAction']); last=st
    if n%5==0: run('B',"shot('/tmp/z4047/t/f-B-%03d.png')"%n)
    n+=1
    if b['cs']==100 and b['clear']: break
    time.sleep(1)
time.sleep(3)
b=kd('B'); a=kd('A')
print('B end', {k:b[k] for k in ('act','cs','bc','clear','csAction','warps','hearts','link')})
print('A end', {k:a[k] for k in ('act','cs','bc','clear','csAction','warps','hearts')})
run('B',"shot('/tmp/z4047/t/f-B-end.png')"); run('A',"shot('/tmp/z4047/t/f-A-end.png')")

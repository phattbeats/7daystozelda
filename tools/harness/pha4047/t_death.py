exec(open('/tmp/z4047/t/drv.py').read())
t0=time.time(); last=None; n=0
while time.time()-t0<100:
    a=kd('A'); b=kd('B')
    st=(a['cs'],b['cs'],a['clear'],b['clear'])
    if st!=last:
        print(round(time.time()-t0,1),'A cs',a['cs'],'t',a['t1DA'],'clear',a['clear'],'| B cs',b['cs'],'t',b['t1DA'],'clear',b['clear'], 'links', [round(v) for v in a['link']], [round(v) for v in b['link']])
        last=st
    if n%6==0:
        run('A',"shot('/tmp/z4047/t/d-A-%03d.png')"%n); run('B',"shot('/tmp/z4047/t/d-B-%03d.png')"%n)
    n+=1
    if a['cs']==100 and b['cs']==100 and a['clear'] and b['clear'] and time.time()-t0>5:
        time.sleep(3); run('A',"shot('/tmp/z4047/t/d-A-end.png')"); run('B',"shot('/tmp/z4047/t/d-B-end.png')"); break
    time.sleep(1)
print('final A', {k:a[k] for k in ('act','cs','bc','clear','csAction')}, 'B', {k:b[k] for k in ('act','cs','bc','clear','csAction')})

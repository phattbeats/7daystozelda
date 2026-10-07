import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
import time
def st(c): return json.loads(run(c,"d=TW(); print(json.dumps(d))"))
both("for i in range(6): key('x'); time.sleep(0.8)\nprint('ok')")
run('A',"TW(1,2)")
t0=time.time()
for i in range(60):
    a=st('A'); b=st('B')
    f=lambda d:(d['stage'],d['csAction'],d['kotake']['act'],d['koume']['act'],d['twinrova']['act'],d['twinrova']['cs2'],d['twinrova']['sup'],d['kotake']['sup'])
    print('%5.1f'%(time.time()-t0),'A',f(a),'B',f(b))
    if a['stage']>=3 and b['stage']>=3: break
    time.sleep(2)

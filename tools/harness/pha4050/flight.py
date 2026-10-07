exec(open('/tmp/z4050/t/vf.py').read())
if ROLE=='B':
    for i in range(80):
        d=VF(0)
        if d['face']==0 and d['fd2Act']=='idle' and d['fd2Pos'][1]>100: VF(1); print('hammer sent'); break
        time.sleep(0.4)
t0=time.time(); n=0
while time.time()-t0<70:
    d=VF(0)
    print(int(time.time()-t0),json.dumps({k:d.get(k) for k in ('hp','fdWait','fd2Wait','fd2Act','face','fdPos','rocks','rockT','fireT','burning','handoff','supFd','dyingFd')}))
    if d['fdWait']==False and n in (0,6,12): shot('/tmp/z4050/t/fl-%s-%d.png'%(ROLE,n)); n+=1
    elif d['fdWait']==False and n<12: n+=1
    time.sleep(1.5)

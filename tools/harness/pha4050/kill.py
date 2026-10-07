exec(open('/tmp/z4050/t/vf.py').read())
t0=time.time(); last=None
if ROLE=='A': VF(3,4)
while time.time()-t0<90:
    d=VF(2 if ROLE=='B' and False else 0)
    if ROLE=='B' and d.get('face')==1 and d.get('hp',0)>0 and d.get('phase')==1: d=VF(2)
    k=(d.get('phase'),d.get('hp'),d.get('fd2Act'),d.get('fd2Death'),d.get('intro'),d.get('present'))
    if k!=last: print(int(time.time()-t0),json.dumps({x:d.get(x) for x in ('present','phase','hp','fd2Act','fd2Death','supFd','supFd2','dyingFd','hearts','warps','clear','scene','health')})); last=k
    if d.get('warps',0)>0 and d.get('clear'): print('DONE',int(time.time()-t0)); break
    time.sleep(0.4)
shot('/tmp/z4050/t/k-%s-end.png'%ROLE)

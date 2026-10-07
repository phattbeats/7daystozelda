exec(open('/tmp/z4050/t/vf.py').read())
t0=time.time(); last=None; walked=False
while time.time()-t0<150:
    d=S(('phase','hp','intro','fd2Act','fd2Death','supFd','supFd2','pending','warps','clear','link'))
    k=(d.get('phase'),d.get('hp'),d.get('intro'),d.get('fd2Act'),d.get('fd2Death'),d.get('pending'),d.get('warps'))
    if k!=last: print(int(time.time()-t0),json.dumps(d)); last=k
    if time.time()-t0>75:
        if d.get('intro') in (None,1): hold('w',1.0)
        key('x',0.08)
    if d.get('warps') and d.get('clear'): print('B DONE'); break
    time.sleep(0.5)
shot('/tmp/z4050/t/def-B-end.png')

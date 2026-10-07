exec(open('/tmp/z4050/t/vf.py').read())
TAG='A'
exec(open('/tmp/z4050/t/intro.py').read().split("exec(open('/tmp/z4050/t/vf.py').read())\n",1)[1])
time.sleep(4)
VF(3,3)
t0=time.time()
while time.time()-t0<60:
    d=VF(0)
    if d['phase']==2: print('A defeated',int(time.time()-t0)); break
    if d['face']==0 and d['fd2Act']=='idle': VF(1)
    elif d['face']==1: VF(2)
    time.sleep(0.5)
for i in range(30):
    d=VF(0); print(i, d['phase'],d['hp'],d['fd2Act'],d['fd2Death'],d['warps'],d['clear']); 
    if d['warps']: break
    time.sleep(2)
shot('/tmp/z4050/t/def-A-end.png')

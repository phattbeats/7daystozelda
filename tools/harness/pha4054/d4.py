exec(open('/tmp/z4054/t/gd.py').read())
import math
G2(10,0)
if NAME=='A': G2(9,0)
t0=time.time(); last=None
while time.time()-t0<300:
    g=G2()
    if g.get('cs')==6 and g.get('phase')==2:
        hx,hy,hz=g['head']; lx,lz=hx+45,hz
        yaw=int(math.atan2(hx-lx,hz-lz)/math.pi*0x8000)
        W(lx,1086.0,lz,yaw&0xffff)
        key('c',0.2)
    else: key('x',0.1)
    st=(g.get('scene'),g.get('phase'),g.get('cs'),g.get('csAction'))
    if st!=last: print(int(time.time()-t0),st,flush=True); last=st
    if g.get('scene')!=79: break
    time.sleep(0.4)
shot('/tmp/z4054/t/ev/d4-%s.png'%NAME)

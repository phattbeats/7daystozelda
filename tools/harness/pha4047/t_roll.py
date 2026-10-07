exec(open('/tmp/z4047/t/drv.py').read())
import math
C=[(-1390,-3804),(-1390,-2804),(-390,-2804),(-390,-3804)]
res={'A':None,'B':None}
for attempt in range(4):
    t0=time.time()
    while time.time()-t0<25:
        a=kd('A')
        if a['act']=='roll' and a['t1DA']>8: break
        if a['act']=='walk' and a['t1DA']==0:
            pass
        time.sleep(0.1)
    else:
        print('no roll yet', a['act']); continue
    tx,tz=C[a['corner']]; x,_,z=a['pos']; d=math.hypot(tx-x,tz-z) or 1
    ux,uz=(tx-x)/d,(tz-z)/d
    off=min(300,d-120)
    yaw=int(math.atan2(-ux,-uz)*32768/math.pi)&0xffff
    # side by side across the lane, so the ball runs over both
    px,pz=-uz,ux
    both("W(%f,-1504,%f,%d)\n"%(x+ux*off+px*35,z+uz*off+pz*35,yaw), "W(%f,-1504,%f,%d)\n"%(x+ux*off-px*35,z+uz*off-pz*35,yaw))
    t1=time.time(); shotA=shotB=False
    while time.time()-t1<5:
        A=kd('A'); B=kd('B')
        for c,s in (('A',A),('B',B)):
            if s['iframes']>0 and res[c] is None:
                res[c]=(attempt, s['iframes'], [round(v) for v in s['link']], s['health'])
                run(c,"shot('/tmp/z4047/t/s05-%s-rollhit.png')"%c)
        if res['A'] and res['B']: break
        time.sleep(0.1)
    print('attempt',attempt,res)
    if res['A'] and res['B']: break
a=kd('A'); b=kd('B'); print('A',a['act'],a['hp'],'B',b['act'],b['hp'],b['sup'])

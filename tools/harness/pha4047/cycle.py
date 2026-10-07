exec(open('/tmp/z4047/t/drv.py').read())
import math
C=[(-1390,-3804),(-1390,-2804),(-390,-2804),(-390,-3804)]
other={'A':'B','B':'A'}
def lane_place(who):
    while True:
        a=kd('A')
        if a['hp']<=0: return a
        if a['act']=='walk':
            tx,tz=C[a['corner']]; x,_,z=a['pos']; d=math.hypot(tx-x,tz-z)
            if d>520: break
        time.sleep(0.3)
    ux,uz=(tx-x)/d,(tz-z)/d
    yaw=int(math.atan2(-ux,-uz)*32768/math.pi)&0xffff
    cx,cz=-890,-3304; ox,oz=2*cx-(x+tx)/2, 2*cz-(z+tz)/2
    run(who,"W(%f,-1504,%f,%d)\n"%(x+ux*300,z+uz*300,yaw)); run(other[who],"W(%f,-1504,%f,0)\n"%(ox,oz))
    return a
def cycle(swallower, hitter, real=True):
    ev=[]
    a=lane_place(swallower)
    hp0=a['hp']; t0=time.time(); fed=False
    while time.time()-t0<30:
        a=kd('A'); s=kd(swallower)
        if a['act']=='inhale' and s['ih'] and 30<=a['ic']<=66 and not fed:
            kd(swallower,1); fed=True; ev.append(('bomb fed by',swallower,'A ic',a['ic'],'%s ic'%swallower,s['ic']))
        if a['act'] in ('laydown','vulnerable'): break
        if a['act'] in ('fire','roll') and fed: break
        time.sleep(0.15)
    a=kd('A'); ev.append(('after feed: A act',a['act'],'hp',a['hp']))
    run('A',"shot('/tmp/z4047/t/c-%s-A-down.png')"%swallower); run('B',"shot('/tmp/z4047/t/c-%s-B-down.png')"%swallower)
    if a['act'] not in ('laydown','vulnerable','explode'):
        return ev
    while kd('A')['act']=='explode': time.sleep(0.1)
    # hitter walks up to the head and swings
    h=kd(hitter); mx,my,mz=h['mouth']; kx,_,kz=h['pos']
    d=math.hypot(mx-kx,mz-kz) or 1; ux,uz=(mx-kx)/d,(mz-kz)/d
    yaw=int(math.atan2(-ux,-uz)*32768/math.pi)&0xffff
    hp1=kd('A')['hp']; how=None
    if real:
        for dist in (55,40,70,30):
            run(hitter,"W(%f,-1504,%f,%d)\nkey('z',0.1)\n"%(mx+ux*dist,mz+uz*dist,yaw))
            for i in range(2):
                run(hitter,"key('c',0.1); time.sleep(0.45)\n")
                if kd('A')['hp']<hp1: how='sword swing (dist %d)'%dist; break
            if how: break
    if not how:
        kd(hitter,2); time.sleep(0.5)
        if kd('A')['hp']<hp1: how='test-hook hit (cmd 2)'
    run(hitter,"shot('/tmp/z4047/t/c-%s-hit.png')"%hitter)
    a=kd('A'); b=kd('B')
    ev.append(('hit by',hitter,how,'hp',hp1,'->',a['hp'],'B sees hp',b['hp'],'A act',a['act']))
    return ev

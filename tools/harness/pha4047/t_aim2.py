exec(open('/tmp/z4047/t/drv.py').read())
import math
C=[(-1390,-3804),(-1390,-2804),(-390,-2804),(-390,-3804)]
def place():
    while True:
        a=kd('A')
        if a['act']=='walk':
            tx,tz=C[a['corner']]; x,_,z=a['pos']
            d=math.hypot(tx-x,tz-z)
            if d>500: break
        time.sleep(0.3)
    ux,uz=(tx-x)/d,(tz-z)/d
    bx,bz=x+ux*320,z+uz*320
    yaw=int(math.atan2(-ux,-uz)*32768/math.pi)&0xffff
    # A: midpoint of the opposite wall
    cx,cz=-890,-3304
    ax,az=2*cx-(x+tx)/2, 2*cz-(z+tz)/2
    both("W(%f,-1504,%f,0)\n"%(ax,az), "W(%f,-1504,%f,%d)\n"%(bx,bz,yaw))
    return a
a=place(); print('placed; KD', [round(v) for v in a['pos']], 'corner', a['corner'])
log=[]; t0=time.time(); burnB=False
while time.time()-t0<20:
    a=kd('A'); b=kd('B')
    log.append((round(time.time()-t0,1), a['act'], a['ih'], a['ic'], a['fl'], a['flames'], a['burning'], '|B', b['ih'], b['fl'], b['flames'], b['burning'], b['sup'], [round(v) for v in b['link']]))
    if b['flames']>3 and not burnB:
        run('A',"shot('/tmp/z4047/t/s04-A-fire.png')"); run('B',"shot('/tmp/z4047/t/s04-B-fire.png')")
    if b['burning']: burnB=True
    if burnB and a['act']!='fire': break
    time.sleep(0.25)
for l in log: print(l)
print('B burned:', burnB)

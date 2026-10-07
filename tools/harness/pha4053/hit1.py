import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
import math,time
def st(c): return json.loads(run(c,"d=TW(); print(json.dumps(d))"))
who=sys.argv[1] if len(sys.argv)>1 else 'B'
other='A' if who=='B' else 'B'
both("for i in range(6): key('x'); time.sleep(0.8)\nprint('ok')")
# Koume shoots; Kotake is parked 30 degrees off her line, in front of the reflector
run('A',"TW(9,1)"); time.sleep(0.4)
d=st('A'); wp=d['koume']['pos']
n=math.hypot(wp[0],wp[2]) or 1
rx,rz=wp[0]/n*170,wp[2]/n*170
ang=math.atan2(wp[0]-rx,wp[2]-rz)            # direction reflector->Koume
kang=ang+math.radians(30)
kx,kz=rx+math.sin(kang)*330, rz+math.cos(kang)*330
yaw=int(kang*32768/math.pi)
print('koume',wp,'reflector',(rx,rz),'kotake pin',(kx,kz),'yaw',yaw)
run('A',"""ev('(()=>{ if(window._pin) clearInterval(window._pin); window._pin=setInterval(()=>Module.ccall("anchor_test_tw_pos",null,["number","number","number","number"],[0,%f,262,%f]),40); setTimeout(()=>clearInterval(window._pin),9000); })()')"""%(kx,kz))
run(who,"W(%f,240,%f,%d)"%(rx,rz,yaw))
run(other,"W(%f,240,%f,0)"%(-rx*1.3,-rz*1.3))
run(who,"page.keyboard.down('r')")
for i in range(26):
    time.sleep(0.4)
    a=st('A'); b=st('B'); k=a['koume']; o=a['kotake']; ob=b['kotake']
    if k['bst']==1 and not globals().get('shot_done'):
        shot_done=True; time.sleep(0.5)
        for c in 'AB': run(c,"shot('/tmp/z4053/t/ev/reflect-by%s-%s.png')"%(who,c))
    print('%4.1f'%(i*0.4),'koume',k['act'],k['bst'],round(k['bd']),k['refl'],'rY',round(k['rYaw'],2),'rD',round(k['rDist']),'| kotake A',o['act'],o['hp'],'B',ob['act'],ob['hp'],'| pools',a['pools'],b['pools'],'burn',a['burning'],b['burning'])
run(who,"page.keyboard.up('r')")

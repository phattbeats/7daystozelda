import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
import math,time
who=sys.argv[1] if len(sys.argv)>1 else 'B'
btype=int(sys.argv[2]) if len(sys.argv)>2 else 1
pre=int(sys.argv[3]) if len(sys.argv)>3 else 2   # charge already on the shield
other='A' if who=='B' else 'B'
def st(c): return json.loads(run(c,"d=TW(); print(json.dumps(d))"))
both("for i in range(4): key('x'); time.sleep(0.6)\nprint('ok')")
d=st('A'); tp=[0,300,575]; print('twinrova',d['twinrova']['pos'],d['twinrova']['act'])
sg=1 if tp[2]>0 else -1
rx,rz=0,-200*sg
yaw=int(math.atan2(tp[0]-rx,tp[2]-rz)*32768/math.pi)
run(who,"TW(5,%d); W(%f,240,%f,%d)"%(pre if btype==1 else pre<<4,rx,rz,yaw))
run(other,"W(0,240,%d,%d)"%(-300*sg,yaw))
run(who,"page.keyboard.down('r')")
run('A',"""ev('(()=>{ if(window._pin) clearInterval(window._pin); window._pin=setInterval(()=>Module.ccall("anchor_test_tw_pos",null,["number","number","number","number"],[2,%f,285,%f]),40); setTimeout(()=>clearInterval(window._pin),9000); })()')"""%(tp[0],tp[2]))
time.sleep(0.3)
run('A',"TW(11,%d)"%btype)
for i in range(34):
    time.sleep(0.25)
    a=st('A'); b=st('B'); ta=a['twinrova']
    bl=a.get('blast') or {}; bb=b.get('blast') or {}
    print('%4.2f'%(i*0.25),'A',ta['act'],ta['hp'],'B',b['twinrova']['act'],b['twinrova']['hp'],'blast',bl.get('cs1'),'chg',b['shieldCharge'])
run(who,"page.keyboard.up('r')")

if len(sys.argv)>4 and sys.argv[4]=='dmg':
    # wait for the stun, then fight her with the sword from both clients
    for i in range(60):
        a=st('A')
        if a['twinrova']['act']==16: break
        time.sleep(0.25)
    print('stunned?',a['twinrova']['act'])
    run('A',"""ev('(()=>{ if(window._pin) clearInterval(window._pin); window._pin=setInterval(()=>Module.ccall("anchor_test_tw_pos",null,["number","number","number","number"],[2,0,262,150]),40); setTimeout(()=>clearInterval(window._pin),30000); })()')""")
    run('A',"W(0,240,95,0)"); run('B',"W(25,240,100,0)")
    time.sleep(1)
    hist=[]
    for rnd in range(8):
        who2='A' if rnd%2==0 else 'B'
        run(who2,"key('c',0.12); time.sleep(0.5)")
        a=st('A'); b=st('B')
        print('swing by',who2,'-> hp A',a['twinrova']['hp'],'B',b['twinrova']['hp'],'act A',a['twinrova']['act'],'B',b['twinrova']['act'])

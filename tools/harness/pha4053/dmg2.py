import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
import time
def st(c): return json.loads(run(c,"d=TW(); print(json.dumps(d))"))
n=int(sys.argv[1]) if len(sys.argv)>1 else 8
run('A',"""ev('(()=>{ if(window._pin) clearInterval(window._pin); window._pin=setInterval(()=>Module.ccall("anchor_test_tw_pos",null,["number","number","number","number"],[2,0,262,150]),40); setTimeout(()=>clearInterval(window._pin),40000); })()')""")
time.sleep(0.5)
run('A',"TW(7)")
for i in range(40):
    a=st('A')
    if a['twinrova']['act']==16: break
    time.sleep(0.25)
print('stun act',a['twinrova']['act'],'hp',a['twinrova']['hp'])
run('A',"W(0,240,95,0)"); run('B',"W(25,240,100,0)")
time.sleep(1)
for rnd in range(n):
    who2='A' if rnd%2==0 else 'B'
    run(who2,"key('c',0.12); time.sleep(1.25)")
    a=st('A'); b=st('B')
    print('swing by',who2,'-> hp A',a['twinrova']['hp'],'B',b['twinrova']['hp'],'act A',a['twinrova']['act'],'B',b['twinrova']['act'],'stage',a['stage'],b['stage'])

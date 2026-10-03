import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def S(): return json.loads(ev('Module.ccall("sevendays_test_state","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def clear():
    z0=0
    for i in range(30):
        if B().get('msg')==0:
            z0+=1
            if z0>=3: return
        else: z0=0; key('x')
        time.sleep(0.6)
raid('warp:211'); time.sleep(10); clear()
ev('Module.ccall("sevendays_test_pack",null,["number"],[9])'); time.sleep(1.5)
print('place',ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[5,-170,-80,640,0])')); time.sleep(1.5)
print([p for p in B()['placeables'] if p['type'] in (5,6)])
V=(150,0,560,-0x3000)
W(*V); time.sleep(1); key('z',0.3); time.sleep(0.5); key('z',0.3); clear()
shot('/tmp/m10ev/vq-z-pre.png')
raid('force'); time.sleep(0.3); raid('dusk')
log=open('/tmp/m10web/vq/raid5.log','w'); after=0
for t in range(150):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    line='%d %s %s pieces=%s link=%s hp=%d msg=%d'%(t,s['wave']['status'],[(r['id'],r['hp'],r['pos']) for r in s['raiders']],pcs,[int(v) for v in b['link'][:3]],s['health'],b['msg'])
    log.write(line+'\n'); log.flush()
    if t%5==0 or len(pcs)<2: print(line[:240],flush=True)
    if s['health']<32: raid('heal')
    if b['msg']: key('x')
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>70: W(*V); time.sleep(0.5); key('z',0.2)
    shot('/tmp/m10ev/vq-z%03d.png'%t); time.sleep(0.8)
    if len(pcs)<2: after+=1
    if after>30 or (s['wave']['status'] in ('cleared','none') and t>12): break
print('end',t)

import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
raid('dawn'); 
for i in range(40):
    s=json.loads(rs())
    if not s['night'] and not s['transition']: break
    time.sleep(1)
print('day', s['day'], s['wave']['status'])
for i in range(8):
    if B().get('msg')==0: break
    key('x'); time.sleep(0.7)
VS=(-170,-80,920,-32768); VB=(80,-80,1100,-0x6000); V=VS
W(*V); time.sleep(0.3); key('z',0.3)
raid('force'); time.sleep(0.3); raid('dusk')
log=open('/tmp/m10web/vq/raid3.log','w')
broke_at=None
for t in range(75):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    line='%d %s %s pieces=%s link=%s hp=%d msg=%d'%(t,s['wave']['status'],[(r['id'],r['hp'],r['pos']) for r in s['raiders']],pcs,[int(v) for v in b['link'][:3]],s['health'],b['msg'])
    print(line,flush=True); log.write(line+'\n'); log.flush()
    if s['health']<32: raid('heal')
    if b['msg']: key('x')
    if 10 not in pcs and V is VS: V=VB; W(*V); key('z',0.2); broke_at=t
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>60: W(*V); key('z',0.2)
    shot('/tmp/m10ev/vq-x%02d.png'%t); time.sleep(1.2)
    if s['wave']['status']=='cleared' and t>5: break
print('broke_at',broke_at)

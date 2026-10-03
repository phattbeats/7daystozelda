import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
V=(150,0,560,-0x3000)
W(*V); time.sleep(1); key('z',0.3)
raid('force'); time.sleep(0.3); raid('dusk')
log=open('/tmp/m10web/vq/raid4.log','w'); after=0
for t in range(120):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    line='%d %s %s pieces=%s link=%s hp=%d msg=%d'%(t,s['wave']['status'],[(r['id'],r['hp'],r['pos']) for r in s['raiders']],pcs,[int(v) for v in b['link'][:3]],s['health'],b['msg'])
    print(line[:300],flush=True); log.write(line+'\n'); log.flush()
    if s['health']<32: raid('heal')
    if b['msg']: key('x')
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>70: W(*V); time.sleep(0.5); key('z',0.2)
    shot('/tmp/m10ev/vq-y%03d.png'%t); time.sleep(1.0)
    if 11 not in pcs: after+=1
    if after>25 or (s['wave']['status']=='cleared' and t>5): break

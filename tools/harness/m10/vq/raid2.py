import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
V=(-280,-80,1060,0x3a00)
W(*V); time.sleep(0.3); key('z',0.3)
raid('force'); time.sleep(0.3); raid('dusk')
log=open('/tmp/m10web/vq/raid2.log','a')
for t in range(50):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    line='%d %s %s pieces=%s link=%s hp=%d msg=%d'%(t,s['wave']['status'],[(r['id'],r['hp'],r['pos'],r['toBench']) for r in s['raiders']],pcs,[int(v) for v in b['link'][:3]],s['health'],b['msg'])
    print(line,flush=True); log.write(line+'\n'); log.flush()
    if s['health']<32: raid('heal')
    if b['msg']: key('x')
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>60: W(*V); key('z',0.2)
    shot('/tmp/m10ev/vq-w%02d.png'%t); time.sleep(1.5)
    if s['wave']['status'] in ('cleared',) and t>5: break

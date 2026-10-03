import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
V=(150,0,560,-0x3000)
W(*V); time.sleep(1); key('z',0.3); time.sleep(0.5); key('z',0.3)
s=json.loads(rs())
if s['wave']['status'] not in ('assault','incoming'): raid('force'); time.sleep(0.3); raid('dusk')
log=open('/tmp/m10web/vq/film.log','w'); after=0
for t in range(140):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    line='%d %s %s pieces=%s hp=%d msg=%d over=%s'%(t,s['wave']['status'],[(r['id'],r['hp'],r['pos']) for r in s['raiders']],pcs,s['health'],b['msg'],s.get('gameOver'))
    log.write(line+'\n'); log.flush(); print(line[:230],flush=True)
    if b['msg']: key('x')
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>70: W(*V); time.sleep(0.5); key('z',0.2)
    shot('/tmp/m10ev/vq-f%03d.png'%t); time.sleep(0.6)
    if 11 not in pcs: after+=1
    if after>45 or (s['wave']['status'] in ('cleared','none') and t>8): break

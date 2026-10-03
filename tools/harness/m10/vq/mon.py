import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
for i in range(8):
    if B().get('msg')==0: break
    key('x'); time.sleep(0.7)
for t in range(14):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    print(t, s['wave']['status'], [(r['id'],r['hp'],r['pos']) for r in s['raiders']], 'pieces',pcs, 'link',[int(v) for v in b['link'][:3]], 'hp',s['health'], 'msg',b['msg'], flush=True)
    if s['health']<24: raid('heal')
    if b['msg']: key('x')
    shot('/tmp/m10ev/vq-r%02d.png'%t); time.sleep(4)

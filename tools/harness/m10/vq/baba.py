import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
for k in range(20):
    if B()['msg']: key('x'); time.sleep(0.6); continue
    s=json.loads(rs())
    if s['raidInterval']!=0: break
    if s['health']<40: raid('heal')
    page.mouse.move(391,205); page.mouse.down(); time.sleep(0.25); page.mouse.up(); time.sleep(1)
print('raidInterval',json.loads(rs())['raidInterval'])
print('craft',ev('Module.ccall("sevendays_test_craft","number",["string"],["guardbaba"])')); time.sleep(1)
print('place',ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[6,-150,-80,760,0])')); time.sleep(1.5)
print([p for p in B()['placeables'] if p['type'] in (5,6)])
V=(150,0,560,-0x3000)
for t in range(40):
    s=json.loads(rs()); b=B()
    pcs={p['id']:p['hp'] for p in b['placeables'] if p['type'] in (5,6)}
    print(t,s['wave']['status'],[(r['hp'],r['pos']) for r in s['raiders']],pcs,s['health'],flush=True)
    if s['health']<40: raid('heal')
    if b['msg']: key('x')
    l=b['link']
    if abs(l[0]-V[0])+abs(l[2]-V[2])>70: W(*V); time.sleep(0.5); key('z',0.2)
    shot('/tmp/m10ev/vq-b%03d.png'%t); time.sleep(0.6)

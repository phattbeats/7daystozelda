import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def S(): return json.loads(ev('Module.ccall("sevendays_test_state","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
raid('calm'); raid('dawn')
for i in range(40):
    s=json.loads(rs())
    if not s['night'] and not s['transition']: break
    time.sleep(1)
for i in range(8):
    if B().get('msg')==0: break
    key('x'); time.sleep(0.7)
print('craft',ev('Module.ccall("sevendays_test_craft","number",["string"],["scarecrow"])')); time.sleep(1)
print('place',ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[5,-170,-80,640,0])')); time.sleep(1.5)
print([p for p in B()['placeables'] if p['type'] in (5,6)])
for i,(x,y,z,yaw) in enumerate(((120,0,640,-0x4000),(150,0,560,-0x4000+0x1000),(-170,0,300,0),(80,-80,1100,-0x6000))):
    W(x,y,z,yaw); time.sleep(1.0); key('z',0.3); time.sleep(0.6); key('z',0.3); time.sleep(1.5); shot('/tmp/m10ev/vq-cam%d.png'%i); print(i,[int(v) for v in B()['link'][:3]])

import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def PA(t): return ev('Module.ccall("sevendays_test_place_ahead","string",["number"],[%d])'%t)
for i in range(4):
    if B().get('msg')==0: break
    key('x'); time.sleep(1)
W(-60,-80,960,16384); time.sleep(1.5); print('baba',PA(6)); time.sleep(1.5)
W(-40,-80,1150,16384); time.sleep(1.5); print('scarecrow',PA(5)); time.sleep(1.5)
b=B(); print([p for p in b['placeables'] if p['type'] in (5,6)], b['placeableActors'])
W(-170,-80,1250,-32768+0); time.sleep(2); shot('/tmp/m10ev/vq-placed.png')

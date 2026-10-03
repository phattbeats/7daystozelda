import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def S(): return json.loads(ev('Module.ccall("sevendays_test_state","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
ev('Module.ccall("sevendays_test_pack",null,["number"],[9])'); time.sleep(1.5); print('kits',S()['kits'])
r=ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[5,-170,-80,640,0])'); time.sleep(1.5)
b=B(); print('placed',r,[p for p in b['placeables'] if p['type'] in (5,6)])
for i,(x,z,yaw) in enumerate(((-170,1350,-32768),(-20,1300,-32768+0x1000),(-330,1250,-32768-0x1000),(80,1100,-32768+0x2000))):
    W(x,-80,z,yaw); time.sleep(0.4); key('z',0.3); time.sleep(1.5); shot('/tmp/m10ev/vq-cam%d.png'%i)
print(json.loads(rs())['wave']['status'])

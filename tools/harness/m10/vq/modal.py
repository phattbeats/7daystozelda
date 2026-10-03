import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
page.mouse.move(391,205); page.mouse.down(); time.sleep(0.2); page.mouse.up(); time.sleep(1)
print(json.loads(rs())['interval'])
for i,(x,z,yaw) in enumerate(((-150,1250,-16384+0x2000),(-250,1050,16384),(200,1250,-0x6000),(150,850,-0x2000+0x8000))):
    W(x,-80,z,yaw); time.sleep(0.4); key('z',0.3); time.sleep(1.5); shot('/tmp/m10ev/vq-cam%d.png'%i)

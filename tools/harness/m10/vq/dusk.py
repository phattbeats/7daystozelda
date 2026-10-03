import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
for i in range(5):
    if B().get('msg')==0: break
    key('x'); time.sleep(1)
W(-330,-80,1050,16384); time.sleep(2); shot('/tmp/m10ev/vq-preraid.png')
ev('Module.ccall("sevendays_test_save",null,[],[])'); ev('window.sohPersist&&window.sohPersist()')
raid('force'); time.sleep(0.5); raid('dusk'); time.sleep(8); shot('/tmp/m10ev/vq-dusk.png')
s=json.loads(rs()); print({k:s[k] for k in ('raidTonight','night','holding','transition','wave')})

import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
z0=0
for i in range(30):
    if B().get('msg')==0:
        z0+=1
        if z0>=3: break
    else: z0=0; key('x')
    time.sleep(0.7)
print('msg',B().get('msg'))
W(-330,-80,1050,16384); time.sleep(0.5); hold('a',0.4); time.sleep(0.3); key('z',0.3); time.sleep(1.5); shot('/tmp/m10ev/vq-view.png')
s=json.loads(rs()); print({k:s[k] for k in ('raidTonight','night','holding','transition')}, s['wave']['status'], len(s['raiders']))

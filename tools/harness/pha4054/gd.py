import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def S(): return json.loads(ev('Module.ccall("sevendays_test_state","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def PA(t): return json.loads(ev('Module.ccall("sevendays_test_place_ahead","string",["number"],[%d])'%t))
def PL(t,x,y,z,r): return ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[%d,%f,%f,%f,%d])'%(t,x,y,z,r))
def KIT(k,n): ev('Module.ccall("sevendays_test_grant_kit",null,["string","number"],["%s",%d])'%(k,n))
def FL(x,z,y): return ev('Module.ccall("sevendays_test_floor","number",["number","number","number"],[%f,%f,%f])'%(x,z,y))
def L(): return [round(v) for v in B()['link'][:3]]
def clearmsg(n=8):
    for i in range(n):
        if B().get('msg')==0: return
        key('x'); time.sleep(0.7)
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def R(): return json.loads(rs())
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def PL(t,x,y,z,r): return ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[%d,%f,%f,%f,%d])'%(t,x,y,z,r))
def FL(x,z,y): return ev('Module.ccall("sevendays_test_floor","number",["number","number","number"],[%f,%f,%f])'%(x,z,y))
def KIT(k,n): ev('Module.ccall("sevendays_test_grant_kit",null,["string","number"],["%s",%d])'%(k,n))
def clearmsg(n=8):
    for i in range(n):
        if B().get('msg')==0: return
        key('x'); time.sleep(0.7)
def GD(cmd=0,arg=0): return json.loads(ev('Module.ccall("anchor_test_gdf","string",["number","number"],[%d,%d])'%(cmd,arg)))
def G2(cmd=0,arg=0): return json.loads(ev('Module.ccall("anchor_test_gn2","string",["number","number"],[%d,%d])'%(cmd,arg)))
def EN(k=0): return ev('Module.ccall("anchor_test_enemies","string",["number"],[%d])'%k)

import json
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

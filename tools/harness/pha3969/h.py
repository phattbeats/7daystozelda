import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def KIT(k,n): ev('Module.ccall("sevendays_test_grant_kit",null,["string","number"],["%s",%d])'%(k,n))
def PA(t): return json.loads(ev('Module.ccall("sevendays_test_place_ahead","string",["number"],[%d])'%t))
def L(): return [round(v) for v in B()['link'][:3]]
def clearmsg(n=8):
    for i in range(n):
        if B().get('msg')==0: return
        key('x'); time.sleep(0.7)
def studio(t,w,turn=0x2000): ev("Module.ccall(\"sevendays_test_icon_studio\",null,[\"number\",\"number\",\"number\"],[%d,%d,%d])"%(t,w,turn))

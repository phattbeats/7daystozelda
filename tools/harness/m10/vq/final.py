import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def S(): return json.loads(ev('Module.ccall("sevendays_test_state","string",[],[])'))
def W(x,y,z,r): ev('Module.ccall("sevendays_test_warp_link",null,["number","number","number","number"],[%f,%f,%f,%d])'%(x,y,z,r))
def clear():
    z0=0
    for i in range(30):
        if B().get('msg')==0:
            z0+=1
            if z0>=3: return
        else: z0=0; key('x')
        time.sleep(0.6)
ev("window.__heal||(window.__heal=setInterval(()=>{try{const s=JSON.parse(Module.ccall('sevendays_test_raid_state','string',[],[]));if(s.health>0&&s.health<40)Module.ccall('sevendays_test_raid',null,['string'],['heal'])}catch(e){}},250))")
raid('warp:211'); time.sleep(10); clear()
for k in range(25):
    if B()['msg']: key('x'); time.sleep(0.6); continue
    if json.loads(rs())['raidInterval']!=0: break
    page.mouse.move(391,205); page.mouse.down(); time.sleep(0.25); page.mouse.up(); time.sleep(1)
print('raidInterval',json.loads(rs())['raidInterval'])
print('kits before',S()['kits'],S()['materials'])
for r in ('scarecrow','guardbaba'): print('craft',r,ev('Module.ccall("sevendays_test_craft","number",["string"],["%s"])'%r)); time.sleep(1)
for pid in [p['id'] for p in B()['placeables'] if p['type'] in (5,6)]:
    ev('Module.ccall("sevendays_test_pack",null,["number"],[%d])'%pid); time.sleep(1)
print('kits',S()['kits'])
ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[5,-170,-80,640,0])'); time.sleep(1.5)
ev('Module.ccall("sevendays_test_place","number",["number","number","number","number","number"],[6,-150,-80,780,0])'); time.sleep(1.5)
print('pieces',[p for p in B()['placeables'] if p['type'] in (5,6)])
s=json.loads(rs()); print('state',s['day'],s['night'],s['wave']['status'])

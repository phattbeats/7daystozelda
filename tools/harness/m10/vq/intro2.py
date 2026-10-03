import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
cv('gSevenDays.Enabled',1)
t0=time.time(); ok=False
while time.time()-t0<400:
    for i in range(6): key('x',0.08); time.sleep(0.2)
    try:
        b=B(); l=b.get('link')
        if b.get('scene') in (52,85) and b.get('msg',1)==0:
            hold('w',1.0); l2=B().get('link')
            if abs(l2[0]-l[0])+abs(l2[2]-l[2])>20: ok=True; break
    except Exception: pass
print('ok',ok,int(time.time()-t0),B().get('scene'),B().get('link'))
ev('Module.ccall("sevendays_test_save",null,[],[])'); time.sleep(1); ev('window.sohPersist&&window.sohPersist()'); time.sleep(3)
print(ev('Module.ccall("sevendays_test_raid_state","string",[],[])')[:300])
print(ev('Module.ccall("sevendays_test_state","string",[],[])')[:300])
shot('/tmp/m10ev/vq-intro.png')

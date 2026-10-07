import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
key('space'); time.sleep(1.5); key('x'); time.sleep(2); key('x'); time.sleep(3)
cv('gSevenDays.Enabled',1)
t0=time.time(); ok=False
while time.time()-t0<420:
    for i in range(6): key('x',0.08); time.sleep(0.2)
    try:
        b=B(); l=b.get('link')
        if b.get('scene') in (52,85) and b.get('msg',1)==0:
            hold('w',1.0); l2=B().get('link')
            if abs(l2[0]-l[0])+abs(l2[2]-l[2])>20: ok=True; break
    except Exception as e: pass
b=B()
print('ok',ok,int(time.time()-t0),b.get('scene'),b.get('link'))
shot('/tmp/z4053/t/ev/intro.png')

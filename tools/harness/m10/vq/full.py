import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
key('space'); time.sleep(3); key('space'); time.sleep(3)
key('x'); time.sleep(2); key('w'); time.sleep(0.4); key('x'); time.sleep(0.4); key('space'); time.sleep(0.8); key('x'); time.sleep(2)
key('x'); time.sleep(2); shot('/tmp/m10ev/vq-fs.png'); key('x'); time.sleep(5)
t0=time.time(); moved=False
while time.time()-t0<400:
    for i in range(6): key('x',0.08); time.sleep(0.2)
    try:
        b=B(); l=b.get('link')
        if l and b.get('msg',1)==0:
            hold('w',1.0); l2=B().get('link')
            if l2 and abs(l2[0]-l[0])+abs(l2[2]-l[2])>20: moved=True; break
    except Exception: pass
print('moved',moved,int(time.time()-t0),B().get('scene'),B().get('link'))
ev('Module.ccall("sevendays_test_save",null,[],[])'); time.sleep(1); ev('window.sohPersist&&window.sohPersist()'); time.sleep(3)
print(ev('JSON.stringify(FS.readdir("/Save"))'))
print(ev('Module.ccall("sevendays_test_raid_state","string",[],[])')[:400])
print(ev('Module.ccall("sevendays_test_state","string",[],[])')[:500])
shot('/tmp/m10ev/vq-intro.png')

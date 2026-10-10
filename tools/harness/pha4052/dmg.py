import time
cv('gCheats.InfiniteHealth',1)
def st():
    d=SST(); return (d.get('hp'),d.get('phase'),d.get('fn'),d.get('timer'),d.get('csAction'),d.get('hearts'),d.get('warps'),d.get('clear'))
page.screenshot(path='/tmp/z4052/ev_fight_%s.png'%NAME)
t0=time.time(); hp0=SST().get('hp'); print(NAME,'start hp',hp0)
if NAME=='B': time.sleep(0.75)
n=0
while time.time()-t0<40:
    d=SST()
    if d.get('phase')==2 or not d.get('present',True): break
    if NAME=='A': SST(3,0); time.sleep(0.2)
    r=SST(2,0); n+=1; time.sleep(1.3)
    print(NAME,int(time.time()-t0),'hit',n,'hp',SST().get('hp'))
print(NAME,'after hits',st(), 'hits',n)
page.screenshot(path='/tmp/z4052/ev_kill_%s.png'%NAME)
t0=time.time()
for i in range(45):
    time.sleep(1.0); s=st()
    if i%4==0 or s[5] or s[2] is None: print(NAME,int(time.time()-t0),s)
    if i in (8,20,32): page.screenshot(path='/tmp/z4052/ev_death_%s_%d.png'%(NAME,i))
    if s[2] is None: break

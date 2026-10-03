import json
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
cv('gSevenDays.Enabled',1)
key('space'); time.sleep(3); key('space'); time.sleep(3); shot('/tmp/m10ev/vq-l0.png'); key('x'); time.sleep(2); key('x'); time.sleep(8)
for i in range(10):
    b=B()
    if b.get('scene') is not None and b.get('msg')==0: break
    key('x'); time.sleep(1)
print(b.get('scene'), b.get('link'), [p for p in b.get('placeables',[]) if p['type'] in (5,6)])
s=json.loads(rs()); print(s['day'],s['night'],s['interval'])
shot('/tmp/m10ev/vq-l1.png')

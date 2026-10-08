import math, json
TAG=globals().get('TAG','base'); ANG=globals().get('ANG',90); DS=globals().get('DS',[900,1700,2500,3300,4200])
HIDE_UI(); heal_guard(True)
for d in DS:
    x=150+d*math.sin(math.radians(ANG)); z=2000+d*math.cos(math.radians(ANG)); y=FL(x,z)
    if y<-1000: print(d,'nofloor'); continue
    yaw=int(math.atan2(-(x-150),-(z-2000))/(2*math.pi)*65536)
    W(x,y+5,z,yaw); time.sleep(3.5)
    for i in range(3): key('z'); time.sleep(0.3)
    time.sleep(1.5)
    f=FPS(2)
    try: dr=json.loads(ev('Module.ccall("sevendays_test_draw","string",["string"],[""])'))
    except Exception as e: dr={}
    page.screenshot(path='/tmp/z4062/ev63/%s_a%d_d%d.png'%(TAG,ANG,d))
    print(TAG,ANG,d,'fps %.1f'%f,{k:dr.get(k) for k in ('tier','zFar','spawned','drawn','farLod','farthestDrawnZ','nearestHiddenZ')},[ (int(p['dist']),p['drawn'],p['tag']) for p in dr.get('puppets',[])])

exec(open('/tmp/vq/h.py').read())
W(-60,0,120,0); time.sleep(1.5); key('z',0.3); time.sleep(0.8)
tr=[]
page.keyboard.down('w')
for i in range(14):
    time.sleep(0.25); tr.append(L())
page.keyboard.up('w'); time.sleep(0.5)
print(tr); print('final',L(), 'floorBg',B().get('floorBgId'))
shot('/tmp/vq/ev/walk1.png')

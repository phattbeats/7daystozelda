exec(open('/tmp/vq/h.py').read())
W(45,0,450,-0x4000); time.sleep(1.5)
tr=[]
page.keyboard.down('w')
for i in range(24):
    time.sleep(0.25); tr.append(L())
    if i==6: shot('/tmp/vq/ev/climb_mid.png')
page.keyboard.up('w'); time.sleep(0.5)
print(tr); print('final',L())
shot('/tmp/vq/ev/climb_end.png')

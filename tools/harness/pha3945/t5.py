exec(open('/tmp/vq/h.py').read())
def walk(x,z,yaw,n,shotat=None,name=''):
    W(x,0,z,yaw); time.sleep(1.2); key('z',0.3); time.sleep(0.8)
    tr=[]
    page.keyboard.down('w')
    for i in range(n):
        time.sleep(0.25); tr.append(L())
        if shotat==i: shot('/tmp/vq/ev/%s.png'%name)
    page.keyboard.up('w'); time.sleep(0.3); return tr
print('door', [ (l[1],l[2]) for l in walk(-300,300,0,10,3,'door_open')])
print('step', [ (l[1],l[2]) for l in walk(210,220,0,8,4,'step')])

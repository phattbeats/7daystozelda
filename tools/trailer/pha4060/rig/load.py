exec(open('/tmp/z4060/t/h.py').read())
key('x'); time.sleep(1.5); key('x'); time.sleep(8)
t0=time.time()
while time.time()-t0<200:
    for i in range(5): key('x',0.08); time.sleep(0.2)
    b=B(); l=b.get('link')
    if l and b.get('msg',1)==0:
        hold('w',0.8); l2=B().get('link')
        if abs(l2[0]-l[0])+abs(l2[2]-l[2])>20: break
print('moving', B().get('scene'), int(time.time()-t0))
exec(open('/tmp/z4060/t/heal.py').read())

exec(open('/tmp/z4050/t/vf.py').read())
t0=time.time()
while time.time()-t0<110:
    d=VF(0)
    print(json.dumps([round(time.time(),2),d['fdWait'],d['fdPos'],d['rocks'],d['rockT'],d['fireT'],d['burning'],d['fd2Wait'],d['hp']]))
    time.sleep(0.1)
shot('/tmp/z4050/t/cf-%s.png'%ROLE)

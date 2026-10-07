p = dict(B=('gateL', 0), C=('gateR', 0), D=('towerE', 0x4000))[CLIENT]
POST(p[0], p[1]); time.sleep(0.5); POST(p[0], p[1]); print(CLIENT, B()['link'])

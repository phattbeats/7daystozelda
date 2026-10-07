import math
N=int(globals().get('N',24))
cv('gRemote.Anchor.HordeMaxAlive', 40)
for i in range(N):
    a=2*math.pi*i/N; r=320+ (i%3)*60
    sid,prm = [(144,16129),(144,16129),(144,16129)][i%3]
    P('anchor_test_mini','spawn:%d,%d,%d,0,%d,0'%(sid,prm,150+r*math.sin(a),2000+r*math.cos(a))); time.sleep(0.2)
time.sleep(2); print('alive', len(R().get('raiders',[])))

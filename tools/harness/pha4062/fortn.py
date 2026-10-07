# fortn.py: candidate pieces for an N-piece base around (150,2000), inside BASE_RADIUS 800 of the workbench.
import math
exec(open('/tmp/z4062/t/fort2.py').read().split("if __name__")[0])
def ring(R, kinds, step=124):
    out=[]; n=max(1,int(0.828*R/step))
    for i in range(8):
        rot=i*0x2000; yaw=i*math.pi/4
        for j in range(n):
            t=(j-(n-1)/2)*step; x,z=P_(R,t,yaw)
            k=kinds[(i*n+j)%len(kinds)]; out.append((k,round(x,1),0.0,round(z,1),rot))
    return out
def candidates(kind='mixed'):
    if kind=='heavy':
        first=[b for b in build() if b[0]=='workbench']
        kinds=['palisade','palisade','palisade','guardbaba','palisade','scarecrow','torch','palisade']
    else:
        first=build(); kinds=['palisade','palisade','deck','torch','palisade','spikes','palisade','stall','barrel','guardbaba']
    out=list(first)
    for R in list(range(790,100,-37)):
        out+=ring(R,kinds,124 if kind!='heavy' else 90)
    return out

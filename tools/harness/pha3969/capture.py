exec(open('h.py').read())
import io
from PIL import Image, ImageChops, ImageStat
O='raw/'
SUBJ={'barricade':0,'spikes':1,'workbench':2,'chest':3,'scarecrow':5,'guardbaba':6,'torch':7,'stonewall':8,'bombtrap':9,'gate':10,'ironwall':11,'palisade':12,'floorplank':13,'floorranch':14,'floorstone':15,'deck':16,'step':17,'ladder':18,'stairs':19,'doorswamp':20,'doormusic':21,'doorpirate':22,'packup':100,'packall':101}
TURN={'stairs':0x6000}  # its treads toward the camera
CLIP={'x':0,'y':0,'width':960,'height':540}
def grab(): return Image.open(io.BytesIO(page.screenshot(clip=CLIP))).convert('RGB')

time.sleep(1)
ONLY=globals().get('ONLY') or list(SUBJ)  # set ONLY=[...] to redo some
for name,t in SUBJ.items():
    if name not in ONLY: continue
    for attempt in range(4):
        for i in range(10):
            if B().get('msg')==0: break
            key('x'); time.sleep(0.8)
        tn=TURN.get(name,0x2000); studio(t,0,tn); time.sleep(0.8); b1=grab(); studio(t,1,tn); time.sleep(0.4); w=grab(); studio(t,0,tn); time.sleep(0.4); b2=grab()
        d=sum(ImageStat.Stat(ImageChops.difference(b1,b2)).mean)
        if d<0.5: break
    b1.save(O+name+'_b.png'); w.save(O+name+'_w.png'); print(name,'diff',round(d,3),'tries',attempt+1)
studio(-1,0)

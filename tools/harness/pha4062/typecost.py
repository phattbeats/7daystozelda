# typecost.py: local driver. Per placeable type: A places 24 of them in a 6x4 grid in front of B, B reports opa/xlu bytes per frame and fps, A packs them again.
import json, subprocess, sys, urllib.request
PORTS = dict(A=19461, B=19462, C=19463, D=19464)
def run(c, code):
    pre = "exec(open('/tmp/z4062/t/h.py').read())\nexec(open('/tmp/z4062/t/gfx.py').read())\nCLIENT=%r\n" % c
    return urllib.request.urlopen(urllib.request.Request('http://127.0.0.1:%d/' % PORTS[c], data=(pre + code).encode()), timeout=900).read().decode().strip()
KITS = ['barricade','spikes','chest','scarecrow','guardbaba','torch','stonewall','bombtrap','ironwall','palisade','floorplank','floorranch','floorstone','deck','step','ladder','stairs','doorswamp','doormusic','doorpirate','chairinn','chairmilkbar','bench','bedinn','bedmayor','dresser','drawers','bookshelf','painting','milkcan','rug','barrel','barrelromani','wagonwheel','stall']
TYPES = {'barricade':0,'spikes':1,'chest':3,'scarecrow':5,'guardbaba':6,'torch':7,'stonewall':8,'bombtrap':9,'ironwall':11,'palisade':12,'floorplank':13,'floorranch':14,'floorstone':15,'deck':16,'step':17,'ladder':18,'stairs':19,'doorswamp':20,'doormusic':21,'doorpirate':22,'chairinn':23,'chairmilkbar':24,'bench':25,'bedinn':26,'bedmayor':27,'dresser':28,'drawers':29,'bookshelf':30,'painting':31,'milkcan':32,'rug':33,'barrel':34,'barrelromani':35,'wagonwheel':36,'stall':37}
N = int(sys.argv[1]) if len(sys.argv) > 1 else 24
print(run('A', "cv('gSevenDays.BaseCap',256)\n" + "".join("KIT(%r,%d)\n" % (k, N + 4) for k in KITS) + "KIT('workbench',2)\nW(150,0,2150,0x8000); time.sleep(0.5); PL(2,150,0,2000,0); time.sleep(1.5)\nprint(len(B()['placeables']))"))
print(run('B', "cv('gSevenDays.GfxPoolScale',4); W(150,0,2750,0x8000); time.sleep(1); W(150,0,2750,0x8000); time.sleep(2)"))
def measure():
    return json.loads(run('B', "g=GFX('reset'); time.sleep(0.2); f=FPS(3); g=GFX(); print(json.dumps(dict(fps=round(f,1), opa=g['opa']['peakUsed'], xlu=g['xlu']['peakUsed'], opaHead=g['opa']['peakHead'], opaTail=g['opa']['peakTail'], skips=g['actorSkips']+g['placeableSkips'])))").splitlines()[-1])
base = measure(); print('base (workbench only)', base)
out = {}
for k in KITS:
    t = TYPES[k]
    placed = run('A', ("W(150,0,2150,0x8000); ids0=set(p['id'] for p in B()['placeables'])\n"
                      "for i in range(%d):\n    PL(%d, 150-325+ (i%%6)*130, 0, 2300 + (i//6)*100, 0); time.sleep(0.12)\n"
                      "time.sleep(1.5); new=[p['id'] for p in B()['placeables'] if p['id'] not in ids0 and p['type']==%d]\nprint(len(new))") % (N, t, t)).splitlines()[-1]
    m = measure(); m['placed'] = int(placed); out[k] = m
    print(k, m, flush=True)
    run('A', "for p in B()['placeables']:\n    if p['type']==%d: _cc('sevendays_test_pack', None, ['number'], [p['id']]); time.sleep(0.04)\ntime.sleep(1)" % t)
json.dump(dict(base=base, types=out, n=N), open('/tmp/z4062/t/typecost.json', 'w'), indent=1)
print('DONE')

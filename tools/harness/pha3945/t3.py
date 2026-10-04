exec(open('/tmp/vq/h.py').read())
T={'floorplank':13,'floorranch':14,'floorstone':15,'deck':16,'step':17,'ladder':18,'stairs':19}
W(-60,0,120,0); time.sleep(1.2)
hold('w',1.6); time.sleep(0.6); print('on deck?',L(),B().get('floorBgId'))
r=PA(13); time.sleep(1.5); print('floor from deck',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']])
# walk onto the new floor
hold('w',1.0); time.sleep(0.5); print('after walk',L())
# a ladder from the ground to the new floor's east edge
W(70,0,450,-0x4000); time.sleep(1.2); r=PA(18); time.sleep(1.5); print('ladder',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']])
b=B()
for p in b['placeables']:
    if p['type'] in (13,18): print(p['id'],p['type'],[round(v) for v in p['pos']],p['rot'])

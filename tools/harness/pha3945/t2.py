exec(open('/tmp/vq/h.py').read())
T={'floorplank':13,'floorranch':14,'floorstone':15,'deck':16,'step':17,'ladder':18,'stairs':19,'doorswamp':20,'doormusic':21,'doorpirate':22,'palisade':12}
clearmsg()
def put(name,x,z,yaw,y=0,wait=1.2):
    W(x,y,z,yaw); time.sleep(wait); r=PA(T[name]); time.sleep(1.2); print(name,'link',L(),'->',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']]); return r
put('deck',-60,260,0)
put('stairs',-60,180,0)
put('doorswamp',-300,320,0)
put('doormusic',-180,320,0)
put('doorpirate',-300,170,0)
put('floorplank',100,160,0)
put('floorplank',100,280,0)
put('step',220,220,0)
b=B(); print('actors',b['placeableActors'],'dyna',b['dyna'])
for p in b['placeables']:
    if p['type']>=12: print(p['id'],p['type'],[round(v) for v in p['pos']],p['rot'],p.get('stk'))

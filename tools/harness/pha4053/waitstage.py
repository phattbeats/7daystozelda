import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
want=int(sys.argv[1]); tmo=int(sys.argv[2]) if len(sys.argv)>2 else 120
t0=time.time()
while time.time()-t0<tmo:
    r=[json.loads(run(c,"d=TW(); print(json.dumps({'stage':d['stage'],'cs2':d['twinrova']['cs2'] if d['twinrova'] else None,'act':d['twinrova']['act'] if d['twinrova'] else None,'sup':[d[k]['sup'] for k in ('kotake','koume','twinrova') if d[k]]}))")) for c in 'AB']
    print(int(time.time()-t0), r)
    if all(x['stage']>=want for x in r): break
    time.sleep(4)

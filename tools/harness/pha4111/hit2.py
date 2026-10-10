es=json.loads(ev('Module.ccall("anchor_test_enemies","string",[],[])'))['enemies']
sel=[e for e in es if e['hp']>0 and not e['dying']]
k=sel[-1]['key']
r=ev('Module.ccall("anchor_test_mini","string",["string"],[%s])'%json.dumps(HITARG%k))
print(r[-100:])

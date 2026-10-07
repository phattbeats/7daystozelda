# host-only enemy spawns: SPAWNS = [(id, params, x, z), ...]
for sid, prm, x, z in SPAWNS:
    P('anchor_test_mini', 'spawn:%d,%d,%d,0,%d,0' % (sid, prm, x, z)); time.sleep(0.15)
print('spawned', len(SPAWNS), 'alive', len(R().get('raiders', [])))

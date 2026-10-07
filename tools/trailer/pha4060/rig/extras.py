exec(open('/tmp/z4060/t/fort2.py').read().split("if __name__")[0])
want = [(T[k], x, y, z) for k, x, y, z, r in build()]
n = 0
for p in B()['placeables']:
    if p['scene'] != 81: continue
    if not any(t == p['type'] and abs(x - p['pos'][0]) < 3 and abs(y - p['pos'][1]) < 3 and abs(z - p['pos'][2]) < 3 for t, x, y, z in want):
        _cc('sevendays_test_pack', None, ['number'], [p['id']]); n += 1; time.sleep(0.05)
time.sleep(1); print('packed', n, 'now', len([p for p in B()['placeables'] if p['scene'] == 81]))

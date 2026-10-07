exec(open('/tmp/z4060/t/fort2.py').read().split("if __name__")[0])
from collections import Counter
have = [(p['type'], round(p['pos'][0]), round(p['pos'][1]), round(p['pos'][2])) for p in B()['placeables'] if p['scene'] == 81]
miss = []
for k, x, y, z, r in build():
    if not any(t == T[k] and abs(hx - x) < 3 and abs(hy - y) < 3 and abs(hz - z) < 3 for t, hx, hy, hz in have): miss.append((k, x, y, z, r))
for k, n in Counter(k for k, *_ in miss).items(): KIT(k, n)
for k, x, y, z, r in miss: PL(T[k], x, y, z, r); time.sleep(0.15)
time.sleep(1.5); print('repaired', len(miss), 'now', len([p for p in B()['placeables'] if p['scene'] == 81]))

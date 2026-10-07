# one shot: every client runs this. Globals from the prelude: SHOT (dict), plus CLIENT.
# SHOT = dict(dur=, zone={'A':(x,y,z,yaw),...}, post=False, rec=None, cam=None (CAM args tuple), director=True)
import math, random
S = SHOT; dur = S.get('dur', 12); random.seed(CLIENT + str(time.time()))
if CLIENT == 'B': HIDE_UI()
z0 = S.get('zone', {}).get(CLIENT)
if z0: W(*z0); time.sleep(0.3); W(*z0)
def nearest():
    l = B()['link']; best = None
    for r in R().get('raiders', []):
        d = math.hypot(r['pos'][0] - l[0], r['pos'][2] - l[2])
        if best is None or d < best[0]: best = (d, r)
    return l, best
if CLIENT == 'B' and S.get('cam'): CAM(*S['cam'])
if CLIENT == 'B' and S.get('rec'): REC_START(S['rec'])
t0 = time.time(); zdown = False; n = 0
while time.time() - t0 < dur:
    if B().get('msg'): key('x'); continue
    if CLIENT == 'A' and S.get('director', True):
        r = R()
        if r['night'] and not r['raidTonight'] and r['wave']['spawned'] == 0: raid('force')
    if S.get('idle') or CLIENT in S.get('idle_clients', ()) or time.time() - t0 < S.get('delay', {}).get(CLIENT, 0):
        time.sleep(0.4); continue
    l, nb = nearest()
    if nb and nb[0] < S.get('reach', 450):
        if not zdown and CLIENT != 'B': page.keyboard.down('z'); zdown = True
        if nb[0] > 90 and not S.get('post'): hold('w', min(0.6, nb[0] / 400))
        a = random.random()
        if a < 0.12: page.keyboard.down('c'); time.sleep(0.7); page.keyboard.up('c'); time.sleep(0.3)   # spin attack
        elif a < 0.22: key('x', 0.1); time.sleep(0.4)                                                  # jump attack
        else: key('c', 0.08); time.sleep(0.18)
    else:
        if zdown: page.keyboard.up('z'); zdown = False
        if S.get('roam') and z0 and not S.get('post'):
            hold(random.choice('wwad'), random.uniform(0.3, 0.8))
            if math.hypot(l[0] - z0[0], l[2] - z0[2]) > S.get('roam'): W(*z0)
        else: time.sleep(0.3)
    n += 1
if zdown: page.keyboard.up('z')
out = {'client': CLIENT, 'loops': n}
if CLIENT == 'B' and S.get('rec'): out['frames'] = REC_STOP()
if CLIENT == 'A': r = R(); out.update(night=r['night'], spawned=r['wave']['spawned'], alive=len(r.get('raiders', [])), budget=r['wave']['budget'])
print(out)

exec(open('/tmp/z4060/t/grass.py').read().split("CAM(3")[0])
HIDE_UI()
CAM(3, (150, 75, 260), (X0, 25, Z0), (230, 95, 240), (X0, 25, Z0), 20 * 11, 55, 55, 3)
REC_START('g1_gather'); time.sleep(0.8)
for k, t in [('spin', 0.8), ('w', 0.35), ('spin', 0.8), ('d', 0.3), ('c', 0), ('c', 0), ('a', 0.6), ('spin', 0.8), ('s', 0.4), ('spin', 0.8), ('c', 0)]:
    if k == 'spin': page.keyboard.down('c'); time.sleep(t); page.keyboard.up('c'); time.sleep(0.6)
    elif k == 'c': key('c', 0.08); time.sleep(0.35)
    else: hold(k, t); time.sleep(0.1)
time.sleep(1.0); print('frames', REC_STOP())

pos = dict(A=(80, 309, 640), B=(40, 359, 60), C=(140, 309, 720), D=(90, 309, 800))[CLIENT]
W(pos[0], pos[1], pos[2], 0x8000); time.sleep(0.6); W(pos[0], pos[1], pos[2], 0x8000); time.sleep(0.4)
if CLIENT == 'B':
    HIDE_UI(); CAM(1, (140, 440, -120), (100, 350, 420), (140, 430, -60), (100, 350, 330), 20 * 8, 55, 55, 1)
    REC_START('s1_temple'); time.sleep(0.8)
else: time.sleep(1.0)
time.sleep({'A': 0.0, 'B': 0.3, 'C': 0.6, 'D': 0.9}[CLIENT])
if CLIENT != 'B': hold('w', 2.4); key('z'); time.sleep(0.2); hold('w', 0.6)
else: time.sleep(3.2)
time.sleep(2.0)
if CLIENT == 'B': print('frames', REC_STOP())
print(CLIENT, B()['link'])

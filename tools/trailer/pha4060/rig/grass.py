import math
X0, Z0 = 520, 2950
def plant(pid, params, rings):
    for dist, n, off in rings:
        for i in range(n):
            yaw = int(((i + off) / n) * 65536) & 0xFFFF
            W(X0, 0, Z0, yaw); time.sleep(0.05)
            P('anchor_test_world', 0, pid, params, dist)
plant(0x125, PARAMS, [(70, 6, 0), (130, 9, 0.5)])
W(X0, 0, Z0, 0x8000); time.sleep(0.5)
CAM(3, (200, 70, 230), (X0, 30, Z0), None, None, 1, 55, 55, HUD); time.sleep(1.2); shot('/tmp/z4060/t/g_%d.png' % PARAMS)

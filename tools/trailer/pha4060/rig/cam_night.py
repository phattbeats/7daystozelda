HIDE_UI(); POST('gateL', 0)
CAM(1, (150-1750, 140, 2750), (150+800, 420, 1800), (150-1400, 230, 2650), (150+800, 760, 1700), 20 * 30, 55)
REC_START('r1_nightfall'); t0 = time.time(); red_t = None
while time.time() - t0 < 40:
    if B().get('msg'): key('x')
    n = P('sevendays_test_nights_state')
    if n['red'] > 0.95 and red_t is None: red_t = time.time()
    if red_t and time.time() - red_t > 8: break
    time.sleep(0.5)
print('cam frames', REC_STOP(), int(time.time() - t0))

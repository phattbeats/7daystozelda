# director (owner A): set up a big raid and bring the night
POST('towerN', 0); cv('gSevenDays.RaidClockSpeed', 30)
cv('gSevenDays.TrailerBudget', int(globals().get('BUDGET', 120))); cv('gRemote.Anchor.HordeMaxAlive', 24); cv('gRemote.Anchor.HordeSpawnFrames', 6)
raid('story:12'); _cc('sevendays_test_nights', None, ['string'], ['time:0xA800']); raid('force'); time.sleep(3.5)
raid('dusk'); t0 = time.time()
while time.time() - t0 < float(globals().get('DIR_T', 40)):
    if B().get('msg'): key('x')
    r = R()
    if r['night'] and not r['raidTonight'] and r['wave']['spawned'] == 0: raid('force')
    time.sleep(0.5)
r = R(); print('director', r['night'], r['raidTonight'], r['wave']['budget'], r['wave']['prologue'], r['wave']['spawned'], len(r.get('raiders', [])))

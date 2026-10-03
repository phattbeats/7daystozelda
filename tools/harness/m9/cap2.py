def cap(tag, ids, states=((0,'a'),(3,'b'))):
    for n,lab in states:
        raid('raids:%d'%n); time.sleep(0.5)
        for tid in ids:
            raid('say:%s'%tid); time.sleep(9)
            shot('/tmp/m9ev/%s-%s-%s-1.png'%(tag,lab,tid))
            if tid in ('2053','5066'):
                key('x'); time.sleep(7); shot('/tmp/m9ev/%s-%s-%s-2.png'%(tag,lab,tid))
            for i in range(5): key('x'); time.sleep(0.8)
TOWNS=[('kak','db',['5075','5076','5074','506B','506A','5066','5079','5063','5064','5067']),
 ('mkt','b1',['701E','7020','7022','7015','7055','700E']),
 ('mke','33',['7002','7003']),
 ('cas','138',['7002']),
 ('ran','157',['2048','204A']),
 ('zor','108',['400A','4011','402D','402E']),
 ('gor','14d',['3015','3027']),
 ('dmt','13d',['3026','3027']),
 ('lab','43',['4018']),
 ('lak','102',['4021']),
 ('ger','117',['6069','6019']),
 ('gef','129',['6001','2053'])]
def runall(towns):
    for tag,ent,ids in towns:
        raid('warp:%s'%ent); time.sleep(14); shot('/tmp/m9ev/%s-scene.png'%tag)
        cap(tag,ids)
        print('done',tag,flush=True)

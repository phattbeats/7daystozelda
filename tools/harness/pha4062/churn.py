# churn.py: scene transitions + day/night/blood moon cycling on one client; env ROUNDS, EXPECT (placeable actors expected back in Hyrule Field)
exec(open('/tmp/z4062/t/gfx.py').read())
ROUNDS=int(globals().get('ROUNDS',3)); EXPECT=int(globals().get('EXPECT',0)); log=[]
GFX('reset')
if CLIENT=='A': raid('story:12'); raid('force')
for i in range(ROUNDS):
    raid('warp:db'); time.sleep(9); s1=B().get('scene')
    raid('warp:0x17D'); time.sleep(11); b=B(); log.append((i, s1, b.get('scene'), b.get('placeableActors'), b['dyna']['polys'], b['dyna']['nodes']))
    for t in ('0x4400','0xC000','0x0'):   # day, night (blood moon once the raid is forced), past midnight
        _cc('sevendays_test_nights', None, ['string'], ['time:'+t]); time.sleep(6)
        ns=json.loads(ev('Module.ccall("sevendays_test_nights_state","string",[],[])')); log.append((t, 'moonRed', ns.get('moonRed')))
g=GFX()
print(CLIENT,'rounds',log)
print(CLIENT,'expect',EXPECT,'ok' if all(l[3]==EXPECT for l in log if len(l)==6) else 'MISMATCH','overflow',g['overflowFrames'],'guard',g['guardTrips'],'peakOpa',g['opa']['peakUsed'],'minFree',g['opa']['minFree'],'actorSkips',g['actorSkips'],'pieceSkips',g['placeableSkips'])

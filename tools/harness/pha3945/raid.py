exec(open('/tmp/vq/h.py').read())
ev('window._hg=setInterval(function(){try{var s=JSON.parse(Module.ccall("sevendays_test_raid_state","string",[],[]));if(s.health<40)Module.ccall("sevendays_test_raid",null,["string"],["heal"])}catch(e){}},500)')
W(-60,104,330,0); time.sleep(1)  # up on the deck, out of reach
raid('force'); time.sleep(0.5); raid('dusk')
t0=time.time()
while time.time()-t0<150:
    clearmsg(1); time.sleep(4)
    s=json.loads(rs())
    if s.get('night') and time.time()-t0>40: break
print('night',s.get('night'),'wave?',{k:s.get(k) for k in ('raidActive','wave','raiders','spawned') if k in s})
f0=B()['frames']; time.sleep(60); clearmsg(2)
b=B(); print('frames advanced',b['frames']-f0)
print([(p['id'],p['type'],p['hp']) for p in b['placeables'] if p['type']>=12])
shot('/tmp/vq/ev/raid.png')
print(list(s.keys()))

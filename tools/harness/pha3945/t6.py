exec(open('/tmp/vq/h.py').read())
page.reload(); time.sleep(5)
page.set_input_files('#rom-file','/tmp/mm/oot.o2r'); page.fill('#f-name','Tester')
for _ in range(180):
    if not page.is_disabled('#btn-solo'): break
    time.sleep(1)
page.click('#btn-solo'); time.sleep(25)
cv('gSevenDays.Enabled',1)
key('space'); time.sleep(3); key('space'); time.sleep(3); key('x'); time.sleep(2); key('x'); time.sleep(8)
for i in range(12):
    b=B()
    if b.get('scene') is not None and b.get('msg')==0: break
    key('x'); time.sleep(1)
raid('calm'); raid('dawn'); raid('warp:ee'); time.sleep(10); clearmsg()
for i in range(30):
    if not json.loads(rs())['night']: break
    clearmsg(2); time.sleep(1)
KIT('deck',2); KIT('palisade',2); KIT('floorplank',2); time.sleep(0.5)
W(-60,0,260,0); time.sleep(1.2); r=PA(16); time.sleep(1.5); print('deck',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']])
W(-60,0,180,0); time.sleep(1.2); r=PA(19); time.sleep(1.5); print('stairs',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']])
W(-60,104,330,0); time.sleep(1.2); print('on deck',L())
r=PA(12); time.sleep(1.5); print('palisade on deck',r.get('valid'),r.get('reason'),[round(v) for v in r['pos']])
b=B(); ids=[(p['id'],p['type'],[round(v) for v in p['pos']]) for p in b['placeables'] if p['type'] in (12,16,19)]; print(ids)
deck=[p['id'] for p in b['placeables'] if p['type']==16][-1]
W(150,0,200,-0x3000); time.sleep(1)
ev('Module.ccall("sevendays_test_damage",null,["number","number"],[%d,999])'%deck); time.sleep(2)
b=B(); print('after breaking deck',[(p['id'],p['type'],[round(v) for v in p['pos']]) for p in b['placeables'] if p['type'] in (12,16,19)])

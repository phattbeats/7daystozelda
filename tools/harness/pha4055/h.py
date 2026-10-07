# in-page helpers for the 2-client rig (exec'd inside playd-gpu.py's namespace). rig z4055
import json, time
def _cc(fn, ret, types, args): return ev('Module.ccall(%s,%s,%s,%s)'%(json.dumps(fn), json.dumps(ret), json.dumps(types), json.dumps(args)))
def B(): return json.loads(ev('Module.ccall("sevendays_test_base","string",[],[])'))
def R(): return json.loads(rs())            # sevendays_test_raid_state dict (scene, link, health, msg...)
def W(x,y,z,r=0): _cc('sevendays_test_warp_link',None,['number']*4,[x,y,z,int(r)])
def FL(x,z,y=3000): return _cc('sevendays_test_floor','number',['number']*3,[x,z,y])
def PL(t,x,y,z,r): return _cc('sevendays_test_place','number',['number']*5,[t,x,y,z,int(r)])
def KIT(k,n): _cc('sevendays_test_grant_kit',None,['string','number'],[k,n])
def clearmsg(n=8):
    for i in range(n):
        if B().get('msg')==0: return
        key('x'); time.sleep(0.7)
def kit():
    """Infinite Health, verbose enemy-sync logging, 7DtZ on, Kokiri sword on B, Deku shield (gohma), bomb bag (full bombs), Hookshot on C-left."""
    cv('gCheats.InfiniteHealth',1); cv('gRemote.Anchor.EnemySyncVerbose',1)
    cv('gSevenDays.Enabled',1); cv('gSevenDays.Base',1)
    raid('gohma'); raid('sword'); raid('tier:bomb'); raid('tier:hookshot'); raid('equip:hookshot')
def heal_guard(on=True):
    ev('''(()=>{ if(window._hg) clearInterval(window._hg); window._hg=null; if(%s) window._hg=setInterval(()=>{ try{ const s=JSON.parse(Module.ccall("sevendays_test_raid_state","string",[],[])); if(s.health>0 && s.health<40) Module.ccall("sevendays_test_raid",null,["string"],["heal"]); }catch(e){} }, 300); })()'''%('true' if on else 'false'))
# return types of exported anchor_test_* probes that do not return a JSON string
_PR={'anchor_test_coop_warp':(None,['number']*2),'anchor_test_cs_send':(None,['number']*4),'anchor_test_set_color':(None,['string']*2),
     'anchor_test_load_room':(None,['number']*4),'anchor_test_reflect_nut':('number',['string','number'])}
def P(name,*args):
    """Call any exported probe: P('enemies'), P('enemies',1), P('enemy_detail'), P('colors'), P('floormas','list'), P('kd',1,0), P('reflect_nut','123',160). Numbers->number, str->string. JSON strings are parsed."""
    fn=name if name.startswith('anchor_test_') or name.startswith('sevendays_test_') else 'anchor_test_'+name
    ret,types=_PR.get(fn,('string',None))
    if types is None: types=['string' if isinstance(a,str) else 'number' for a in args]
    r=_cc(fn,ret,types,list(args))
    if isinstance(r,str):
        try: return json.loads(r)
        except Exception: return r
    return r
def cv(n,v): _cc('sevendays_test_cvar',None,['string','number'],[n,v])
def LT(c): raid(c)
def PERSIST(): ev('sohPersist()')

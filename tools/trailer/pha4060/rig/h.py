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
def CAM(mode=0, e0=(0,0,0), a0=(0,0,0), e1=None, a1=None, n=1, fov0=60, fov1=None, hud=1):
    """Trailer camera (capture build): mode 0 off, 1 world path, 2 Link-relative, 3 orbit (eye = angle deg, height, radius)."""
    e1=e1 or e0; a1=a1 or a0; fov1=fov1 if fov1 is not None else fov0
    _cc('sevendays_test_cam',None,['number']*17,[mode,*e0,*a0,*e1,*a1,n,fov0,fov1,hud])
def FPS(t=3):
    return ev('''new Promise(r=>{let n=0;const f=()=>{n++;if(performance.now()-t0<%d)requestAnimationFrame(f);else r(n/%f)};const t0=performance.now();requestAnimationFrame(f)})'''%(t*1000,t))
# --- #4060 recorder: CDP screencast JPEGs + MediaRecorder audio-only, synced by wall clock ---
import base64, os
if '_REC' not in globals():
    _REC = {'on': False}
    time.sleep = lambda s, _w=page.wait_for_timeout: _w(max(0, s) * 1000)  # keep screencast events flowing during scripted waits
def REC_START(name, w=1280, h=720, q=80):
    d = '/tmp/z4060/rec/' + name; os.makedirs(d, exist_ok=True)
    for f in os.listdir(d): os.remove(os.path.join(d, f))
    if page.viewport_size != {'width': w, 'height': h}: page.set_viewport_size({'width': w, 'height': h}); time.sleep(1.5)
    cdp = page.context.new_cdp_session(page)
    st = {'on': True, 'cdp': cdp, 'dir': d, 'n': 0, 'ts': open(d + '/ts.txt', 'w')}
    def onf(p):
        if st['on']:
            open('%s/f%06d.jpg' % (d, st['n']), 'wb').write(base64.b64decode(p['data']))
            st['ts'].write('%d %.4f\n' % (st['n'], p['metadata']['timestamp'])); st['n'] += 1
        try: cdp.send('Page.screencastFrameAck', {'sessionId': p['sessionId']})
        except Exception: pass
    cdp.on('Page.screencastFrame', onf)
    a = ev('''(()=>{ if(!window.__tapCtx) return -1; const s=window.__tapCtx.__tap.stream; const mr=new MediaRecorder(s,{mimeType:'audio/webm;codecs=opus',audioBitsPerSecond:192000});
      let q=Promise.resolve(); mr.ondataavailable=e=>{ if(e.data.size){const b=e.data; q=q.then(()=>fetch('http://127.0.0.1:19460/%s.audio.webm',{method:'POST',body:b}).catch(()=>{}));} };
      mr.onstart=()=>{ window.__audioT0=Date.now()/1000; }; window.__amr=mr; mr.start(1000); return Date.now()/1000; })()''' % name)
    try: os.remove('/tmp/z4060/rec/%s.audio.webm' % name)
    except FileNotFoundError: pass
    cdp.send('Page.startScreencast', {'format': 'jpeg', 'quality': q, 'everyNthFrame': 1, 'maxWidth': w, 'maxHeight': h})
    _REC.clear(); _REC.update(st); return a
def REC_STOP():
    if not _REC.get('on'): return None
    _REC['on'] = False
    try: _REC['cdp'].send('Page.stopScreencast')
    except Exception: pass
    a0 = ev('(()=>{ const t=window.__audioT0||0; if(window.__amr) window.__amr.stop(); return t; })()'); time.sleep(1.5)
    _REC['ts'].close(); open(_REC['dir'] + '/audio_t0.txt', 'w').write('%.4f\n' % (a0 or 0))
    try: _REC['cdp'].detach()
    except Exception: pass
    return _REC['n']
def HIDE_UI():
    ev('''(()=>{ if(document.getElementById('__trl')) return; const s=document.createElement('style'); s.id='__trl'; s.textContent='#net-pill,#bar,#touch-gamepad,#rotate,#copy-note,#bgm,#overlay{display:none!important}'; document.head.appendChild(s); })()''')
POSTS = dict(gateL=(0, 104, 2522), gateR=(300, 104, 2522), towerE=(648, 208, 2206), towerN=(-56, 208, 2498),
             towerS=(356, 208, 1502), towerW=(-348, 208, 1794), bench=(150, 0, 2000))
def POST(name, yaw=0, dx=0, dz=0):
    x, y, z = POSTS[name]; W(x + dx, y, z + dz, yaw)
def CLR(n=40):
    zero = 0
    for i in range(n):
        if B().get('msg'): key('x'); time.sleep(0.5); zero = 0
        else:
            zero += 1
            if zero >= 3: return
            time.sleep(0.2)

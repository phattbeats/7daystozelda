# scen.py: per-client scenario. env: SCALE, DD (draw distance), DUR
exec(open('/tmp/z4062/t/gfx.py').read())
cv('gSevenDays.GfxPoolScale', int(globals().get('SCALE',4))); cv('gEnhancements.DisableDrawDistance', int(globals().get('DD',4)))
time.sleep(3); GFX('reset'); fps=[]
t0=time.time()
while time.time()-t0 < float(globals().get('DUR',40)):
    fps.append(round(FPS(2),1))
g=GFX()
print(CLIENT, 'fps', fps, 'scale',g['scale'],'frames',g['frames'],'overflow',g['overflowFrames'],'guard',g['guardTrips'],'actorSkips',g['actorSkips'],'pieceSkips',g['placeableSkips'])
print(CLIENT, {k:(g[k]['peakUsed'],g[k]['peakHead'],g[k]['peakTail'],g[k]['size'],g[k]['minFree']) for k in ('opa','xlu','ovl')}, 'loopEnd', g['actorLoopOpaUsed'], g['actorLoopXluUsed'])

print(CLIENT, 'mem wasmHeapMB', round(ev('Module.HEAP8.length')/1048576,1), 'jsHeapMB', ev('Math.round((performance.memory||{usedJSHeapSize:0}).usedJSHeapSize/1048576)'), 'arenaFree', B().get('arenaFree'), 'dyna', B().get('dyna'))

# failsafe.py: force N frames to count as overrun (the backstop path) on one client; the tab must stay up and keep drawing
exec(open('/tmp/z4062/t/gfx.py').read())
g0=GFX('reset'); f0=B().get('frames'); g=GFX('force:%d'%int(globals().get('FORCE',60)))
time.sleep(8); g1=GFX(); f1=B().get('frames')
print(CLIENT,'force',g['forceOverflow'],'-> overflowFrames',g1['overflowFrames'],'forceLeft',g1['forceOverflow'],'gameFrames',f0,'->',f1,'fps',FPS(2))

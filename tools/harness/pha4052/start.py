import time
page.goto('http://127.0.0.1:18452/index.html?r=%d#room=sst4052&name=%s'%(int(time.time()),NAME)); time.sleep(40); page.mouse.click(480,270); time.sleep(1)
exec(open('/tmp/z4052/t/load.py').read())
for i in range(8):
    key('x'); time.sleep(1)
print('in', B().get('scene'))

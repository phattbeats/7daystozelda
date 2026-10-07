NAME=globals().get('NAME','A'); ROOM=globals().get('ROOM','tw1')
page.goto('http://127.0.0.1:18453/index.html?r=%d'%int(time.time())); time.sleep(6)
if page.query_selector('#rom-file') and not page.is_disabled('#rom-file') and not ev('typeof Module!=="undefined" && !!Module.FS && Module.FS.readdir("/Save").includes("file1.sav")'):
    pass
page.fill('#f-name', NAME); page.fill('#f-room', ROOM)
for _ in range(120):
    if not page.is_disabled('#btn-join'): break
    time.sleep(1)
page.click('#btn-join'); time.sleep(25); page.reload(); time.sleep(35); page.mouse.click(480,270); time.sleep(1)
exec(open('/tmp/z4053/t/load.py').read())

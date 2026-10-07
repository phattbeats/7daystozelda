# first boot of a fresh profile: ROM, save files, join room fm45
import base64
NAME=globals().get('NAME','A')
for i in range(60):
    if page.query_selector('#rom-file'): break
    time.sleep(1)
page.set_input_files('#rom-file','/tmp/z4006/oot.o2r'); time.sleep(5)
for i in range(90):
    if ev('typeof Module!=="undefined" && !!Module.FS && typeof sohPersist==="function"'): break
    time.sleep(1)
for f in ('file1.sav','global.sav'):
    b=list(open('/tmp/z4051/t/'+f,'rb').read())
    ev('(()=>{try{Module.FS.mkdir("/Save")}catch(e){}; Module.FS.writeFile("/Save/%s", new Uint8Array(%s))})()'%(f,json.dumps(b) if 'json' in globals() else str(b)))
ev('sohPersist()'); time.sleep(3)
print('saves', ev('Module.FS.readdir("/Save")'))
page.fill('#f-name', NAME); page.fill('#f-room','fm45')
for _ in range(120):
    if not page.is_disabled('#btn-join'): break
    time.sleep(1)
page.click('#btn-join'); time.sleep(25); page.reload(); time.sleep(35); page.mouse.click(480,270); time.sleep(1)
exec(open('/tmp/z4051/t/load.py').read())

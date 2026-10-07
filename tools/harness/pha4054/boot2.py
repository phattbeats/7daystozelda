# fresh profile: ROM, saves from A, then join
NAME=globals().get('NAME','B')
for i in range(60):
    if page.query_selector('#rom-file'): break
    time.sleep(1)
page.set_input_files('#rom-file','/tmp/z4054/t/oot.o2r'); time.sleep(5)
for i in range(90):
    if ev('typeof Module!=="undefined" && !!Module.FS && typeof sohPersist==="function"'): break
    time.sleep(1)
for f in ('file1.sav','global.sav'):
    b=list(open('/tmp/z4054/t/'+f,'rb').read())
    ev('(()=>{try{Module.FS.mkdir("/Save")}catch(e){}; Module.FS.writeFile("/Save/%s", new Uint8Array(%s))})()'%(f,str(b)))
ev('sohPersist()'); time.sleep(3)
print('saves', ev('Module.FS.readdir("/Save")'))

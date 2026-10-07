import json,base64
ev('Module.ccall("sevendays_test_save",null,[],[])'); time.sleep(1); ev('window.sohPersist&&window.sohPersist()'); time.sleep(3)
fs=ev('Module.FS.readdir("/Save")'); print(fs)
for f in ('file1.sav','global.sav'):
    b=ev('Array.from(Module.FS.readFile("/Save/%s"))'%f)
    open('/tmp/z4053/t/%s'%f,'wb').write(bytes(b)); print(f,len(b))

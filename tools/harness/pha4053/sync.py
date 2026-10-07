import os,subprocess,sys,shutil
repo='/tmp/z4053/repo'; tree=sys.argv[1]; apply=len(sys.argv)>2 and sys.argv[2]=='apply'
files=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=repo).split(b'\0')
changed=[]
for f in files:
    if not f: continue
    f=f.decode()
    if f.startswith(('docs/','tools/','web/','web-build/','art/')): continue
    s=os.path.join(repo,f); d=os.path.join(tree,f)
    if not os.path.isfile(s): continue
    a=open(s,'rb').read()
    if os.path.exists(d):
        b=open(d,'rb').read()
        if a==b: continue
        if a.replace(b'\r\n',b'\n')==b.replace(b'\r\n',b'\n'): continue
    changed.append(f)
    if apply:
        os.makedirs(os.path.dirname(d),exist_ok=True)
        shutil.copyfile(s,d)
print(len(changed))
for f in changed: print(f)

import os,sys
# usage: fixpaths.py OLD NEW dir...   (same length byte replace, mtimes kept)
old=sys.argv[1].encode(); new=sys.argv[2].encode(); assert len(old)==len(new)
n=0
for d in sys.argv[3:]:
    for root,dirs,files in os.walk(d):
        for f in files:
            p=os.path.join(root,f)
            try:
                if os.path.islink(p): continue
                st=os.stat(p)
                if st.st_size>400_000_000: continue
                data=open(p,'rb').read()
                if old in data:
                    open(p,'wb').write(data.replace(old,new))
                    os.utime(p,ns=(st.st_atime_ns,st.st_mtime_ns))
                    n+=1
            except Exception as e:
                print('ERR',p,e)
print('changed',n)

import urllib.request, lzma, re, os, sys, tarfile, io, collections
BASE='http://deb.debian.org/debian/'
OUT=sys.argv[1]
roots=['libglib2.0-0t64','libnspr4','libnss3','libatk1.0-0t64','libatk-bridge2.0-0t64','libdbus-1-3','libx11-6','libxcomposite1','libxdamage1','libxext6','libxfixes3','libxrandr2','libgbm1','libxcb1','libxkbcommon0','libasound2t64','libatspi2.0-0t64','libglvnd0','libegl1','libgles2','libfontconfig1','fonts-dejavu-core','libgl1','libglx0','libopengl0','libcups2t64','libpango-1.0-0','libcairo2']
skip={'libc6','libgcc-s1','libstdc++6','zlib1g','debconf','dpkg','perl-base','libcrypt1'}
print('fetching index',flush=True)
idx=lzma.decompress(urllib.request.urlopen(BASE+'dists/trixie/main/binary-amd64/Packages.xz').read()).decode()
pk={}; prov=collections.defaultdict(list)
for blk in idx.split('\n\n'):
    d={}
    for ln in blk.split('\n'):
        if ln.startswith(' '): continue
        k,_,v=ln.partition(': '); d[k]=v
    if 'Package' in d:
        pk[d['Package']]=d
        for p in d.get('Provides','').split(','):
            p=p.strip().split(' ')[0]
            if p: prov[p].append(d['Package'])
want=set(); stack=list(roots)
while stack:
    n=stack.pop()
    if n in want or n in skip: continue
    if n not in pk:
        if n in prov: n=prov[n][0]
        else: continue
    if n in want or n in skip: continue
    want.add(n)
    for dep in pk[n].get('Depends','').split(','):
        alt=dep.strip().split('|')[0].strip().split(' ')[0].split(':')[0]
        if alt: stack.append(alt)
print(len(want),'packages',flush=True)
os.makedirs(OUT,exist_ok=True)
def ar_members(b):
    assert b[:8]==b'!<arch>\n'; i=8
    while i<len(b):
        name=b[i:i+16].decode().strip(); size=int(b[i+48:i+58]); data=b[i+60:i+60+size]; yield name,data; i+=60+size+(size&1)
for n in sorted(want):
    url=BASE+pk[n]['Filename']
    b=urllib.request.urlopen(url).read()
    for name,data in ar_members(b):
        if name.startswith('data.tar'):
            if name.endswith('.xz'): data=lzma.decompress(data)
            elif name.endswith('.zst'):
                import zstandard; data=zstandard.ZstdDecompressor().stream_reader(io.BytesIO(data)).read()
            tf=tarfile.open(fileobj=io.BytesIO(data))
            tf.extractall(OUT,filter='fully_trusted')
print('ok',flush=True)

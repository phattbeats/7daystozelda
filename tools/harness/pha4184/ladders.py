import zipfile,struct,sys,collections
z=zipfile.ZipFile('/tmp/z4181/t/oot.o2r')
for name in sys.argv[1:]:
    d=z.read([i for i in z.namelist() if i.startswith('scenes/shared/%s_scene/'%name) and 'CollisionHeader' in i][0])
    o=0x40+12
    nv,=struct.unpack_from('<I',d,o);o+=4
    V=[struct.unpack_from('<3h',d,o+6*i) for i in range(nv)];o+=6*nv
    np_,=struct.unpack_from('<I',d,o);o+=4
    P=[struct.unpack_from('<4H4h',d,o+16*i) for i in range(np_)];o+=16*np_
    nt,=struct.unpack_from('<I',d,o);o+=4
    T=[struct.unpack_from('<2I',d,o+8*i) for i in range(nt)]
    groups=collections.defaultdict(list)
    for p in P:
        w0=T[p[0]][1]; wall=(w0>>21)&0x1F
        if wall in (2,3,4):
            vs=[V[p[k]&0x1FFF] for k in (1,2,3)]
            groups[wall].append(vs)
    for w,g in groups.items():
        print(name,'wall',w,'polys',len(g))
        for vs in g[:8]:
            print('  ',[tuple(v) for v in vs],)

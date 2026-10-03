"""Z64 (OoT/MM) display-list toolkit for 7 Days to Zelda.

- walk(): interprets an F3DEX2 display list, returning textured triangles (for scouting renders).
- Exporter: converts a display list into libultraship o2r resources (DisplayList, Vertex array,
  Texture), mirroring OTRExporter/DisplayListExporter.cpp so the game loads them like extracted assets.
"""
import struct

G_VTX, G_DL, G_ENDDL, G_TRI1, G_TRI2, G_QUAD = 0x01, 0xDE, 0xDF, 0x05, 0x06, 0x07
G_SETTIMG, G_LOADBLOCK, G_LOADTILE, G_LOADTLUT = 0xFD, 0xF3, 0xF4, 0xF0
G_SETTILE, G_SETTILESIZE, G_GEOMETRYMODE, G_SETOTHERMODE_H = 0xF5, 0xF2, 0xD9, 0xE3
G_SETPRIMCOLOR, G_SETENVCOLOR, G_TEXTURE, G_MTX, G_POPMTX = 0xFA, 0xFB, 0xD7, 0xDA, 0xD8
G_RDPHALF_1, G_BRANCH_Z, G_CULLDL, G_SETCOMBINE = 0xE1, 0x04, 0x03, 0xFC
G_SETTIMG_OTR_HASH, G_DL_OTR_HASH, G_VTX_OTR_HASH, G_MARKER = 0x20, 0x31, 0x32, 0x33
G_LIGHTING = 0x00020000

# ---------------------------------------------------------------- CRC64 (libultraship StrHash64)
_T = []
for i in range(256):
    c = i << 56
    for _ in range(8):
        c = ((c << 1) ^ 0x42F0E1EBA9EA3693) if c & (1 << 63) else (c << 1)
        c &= 0xFFFFFFFFFFFFFFFF
    _T.append(c)


def crc64(s):
    crc = 0xFFFFFFFFFFFFFFFF
    for b in s.encode():
        crc = _T[((crc >> 56) ^ b) & 0xFF] ^ ((crc << 8) & 0xFFFFFFFFFFFFFFFF)
    return crc


def header(restype):
    h = struct.pack('<BBBBIIQIQI', 0, 0, 0, 0, restype, 0, 0xDEADBEEFDEADBEEF, 0, 0, 0)
    return h + b'\0' * (0x40 - len(h))


RT_TEXTURE, RT_DL, RT_ARRAY = 0x4F544558, 0x4F444C54, 0x4F415252

# ---------------------------------------------------------------- texture decode (for renders)
BPP = {0: 4, 1: 8, 2: 16, 3: 32}


def tex_type(fmt, siz):
    return {(0, 2): 2, (0, 3): 1, (2, 0): 3, (2, 1): 4, (4, 0): 5, (4, 1): 6,
            (3, 0): 7, (3, 1): 8, (3, 2): 9}.get((fmt, siz), 0)


def _rgba16(v):
    return ((v >> 11) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31, 255 if v & 1 else 0)


def decode_tex(data, fmt, siz, w, h, tlut=None):
    px = []
    n = w * h
    for i in range(n):
        if siz == 0:
            b = data[i // 2] if i // 2 < len(data) else 0
            v = (b >> 4) if i % 2 == 0 else (b & 15)
        elif siz == 1:
            v = data[i] if i < len(data) else 0
        elif siz == 2:
            v = struct.unpack_from('>H', data, i * 2)[0] if i * 2 + 2 <= len(data) else 0
        else:
            v = struct.unpack_from('>I', data, i * 4)[0] if i * 4 + 4 <= len(data) else 0
        if fmt == 0 and siz == 2:
            px.append(_rgba16(v))
        elif fmt == 0 and siz == 3:
            px.append((v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255))
        elif fmt == 2:
            if tlut and v < len(tlut):
                px.append(_rgba16(tlut[v]))
            else:
                px.append((v * 16, v * 16, v * 16, 255))
        elif fmt == 4:
            g = v * 17 if siz == 0 else v
            px.append((g, g, g, g))
        elif fmt == 3:
            if siz == 0:
                g = (v >> 1) * 255 // 7; a = 255 if v & 1 else 0
            elif siz == 1:
                g = (v >> 4) * 17; a = (v & 15) * 17
            else:
                g = v >> 8; a = v & 255
            px.append((g, g, g, a))
        else:
            px.append((255, 0, 255, 255))
    return px


# ---------------------------------------------------------------- DL walker
class Ctx:
    """segs: {segnum: bytes}. names: {segnum: o2r folder for that segment's data}."""

    def __init__(self, segs):
        self.segs = segs

    def read(self, addr, n):
        seg = (addr >> 24) & 0xFF
        off = addr & 0xFFFFFF
        if seg not in self.segs:
            return None
        return self.segs[seg][off:off + n]


def walk(ctx, addr, out=None, depth=0, state=None, tri_filter=None):
    """Returns list of tris: each tri = dict(v=[(x,y,z,s,t,r,g,b,a)]*3, tex=(addr,fmt,siz,w,h,tlut,
    shifts, shiftt, uls, ult), lit=bool, prim=(r,g,b,a))."""
    if out is None:
        out = []
    if state is None:
        state = dict(vbuf=[None] * 64, timg=None, tiles={}, tlut=None, geo=0, prim=(255, 255, 255, 255),
                     env=(255, 255, 255, 255), texon=False, sscale=1.0, tscale=1.0, loads={})
    if depth > 16:
        return out
    pc = addr
    while True:
        cmd = ctx.read(pc, 8)
        if cmd is None or len(cmd) < 8:
            break
        w0, w1 = struct.unpack('>II', cmd)
        op = w0 >> 24
        pc += 8
        if op == G_VTX:
            n = (w0 >> 12) & 0xFF
            v0 = ((w0 >> 1) & 0x7F) - n
            raw = ctx.read(w1, n * 16)
            if raw is not None:
                for i in range(n):
                    x, y, z, f, s, t, r, g, b, a = struct.unpack_from('>hhhHhhBBBB', raw, i * 16)
                    if 0 <= v0 + i < 64:
                        state['vbuf'][v0 + i] = (x, y, z, s, t, r, g, b, a)
        elif op in (G_TRI1, G_TRI2, G_QUAD):
            idx = []
            if op == G_TRI1:
                idx.append(((w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF))
            else:
                idx.append(((w0 >> 16) & 0xFF, (w0 >> 8) & 0xFF, w0 & 0xFF))
                idx.append(((w1 >> 16) & 0xFF, (w1 >> 8) & 0xFF, w1 & 0xFF))
            for half, (a, b, c) in enumerate(idx):
                vs = [state['vbuf'][a // 2], state['vbuf'][b // 2], state['vbuf'][c // 2]]
                if None in vs:
                    continue
                t0 = state['tiles'].get(0)
                tex = None
                if state['texon'] and t0 and t0.get('src'):
                    tex = dict(t0)
                    tex['sscale'] = state['sscale']; tex['tscale'] = state['tscale']
                    if t0['fmt'] == 2:
                        tex['tlut'] = state['tlut']
                out.append(dict(v=vs, tex=tex, lit=bool(state['geo'] & G_LIGHTING), prim=state['prim'],
                                src=(pc - 8, half)))
        elif op == G_DL:
            if (w1 >> 24) in ctx.segs:
                walk(ctx, w1, out, depth + 1, state)
            if (w0 >> 16) & 0xFF == 1:
                break
        elif op == G_ENDDL:
            break
        elif op == G_SETTIMG:
            state['timg'] = dict(addr=w1, fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3, width=(w0 & 0xFFF) + 1)
        elif op == G_SETTILE:
            t = (w1 >> 24) & 7
            tl = state['tiles'].setdefault(t, {})
            tl.update(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3, line=(w0 >> 9) & 0x1FF, tmem=w0 & 0x1FF,
                      cmt=(w1 >> 18) & 3, maskt=(w1 >> 14) & 15, shiftt=(w1 >> 10) & 15,
                      cms=(w1 >> 8) & 3, masks=(w1 >> 4) & 15, shifts=w1 & 15)
            if state['loads'].get(tl['tmem']):
                tl['src'] = state['loads'][tl['tmem']]
        elif op in (G_LOADBLOCK, G_LOADTILE):
            t = (w1 >> 24) & 7
            tl = state['tiles'].get(t, {})
            ti = state['timg']
            if ti:
                src = dict(ti)
                if op == G_LOADBLOCK:
                    texels = ((w1 >> 12) & 0xFFF) + 1
                    src['bytes'] = (texels * BPP[ti['siz']] + 7) // 8
                else:
                    ult = (w0 & 0xFFF) >> 2; lrt = (w1 & 0xFFF) >> 2
                    src['bytes'] = ((lrt + 1) * ti['width'] * BPP[ti['siz']] + 7) // 8
                    src['rowoff'] = ult
                state['loads'][tl.get('tmem', 0)] = src
                for k, o in state['tiles'].items():
                    if o.get('tmem') == tl.get('tmem', 0) and k != t:
                        o['src'] = src
        elif op == G_LOADTLUT:
            ti = state['timg']
            cnt = ((w1 >> 14) & 0x3FF) + 1
            raw = ctx.read(ti['addr'], cnt * 2) if ti else None
            if raw:
                state['tlut'] = list(struct.unpack('>%dH' % (len(raw) // 2), raw))
                state['tlut_addr'] = ti['addr']
        elif op == G_SETTILESIZE:
            t = (w1 >> 24) & 7
            tl = state['tiles'].setdefault(t, {})
            tl['uls'] = (w0 >> 12) & 0xFFF; tl['ult'] = w0 & 0xFFF
            tl['w'] = (((w1 >> 12) & 0xFFF) - tl['uls']) // 4 + 1
            tl['h'] = ((w1 & 0xFFF) - tl['ult']) // 4 + 1
            if 'tmem' in tl and state['loads'].get(tl['tmem']):
                tl['src'] = state['loads'][tl['tmem']]
        elif op == G_GEOMETRYMODE:
            state['geo'] = (state['geo'] & (w0 & 0xFFFFFF | 0xFF000000)) | w1
        elif op == G_TEXTURE:
            state['texon'] = bool(w0 & 0xFE) or True
            state['sscale'] = ((w1 >> 16) & 0xFFFF) / 65536.0
            state['tscale'] = (w1 & 0xFFFF) / 65536.0
            state['texon'] = ((w0 >> 1) & 0x7F) != 0
        elif op == G_SETPRIMCOLOR:
            state['prim'] = ((w1 >> 24) & 255, (w1 >> 16) & 255, (w1 >> 8) & 255, w1 & 255)
    return out


def tri_texture_pixels(ctx, tex):
    """Decode the texture of a tri into (w, h, pixels)."""
    src = tex['src']
    w, h = tex.get('w', 32), tex.get('h', 32)
    fmt, siz = tex['fmt'], tex['siz']
    nbytes = (w * h * BPP[siz] + 7) // 8
    if src.get('rowoff'):
        pass
    data = ctx.read(src['addr'], max(nbytes, 1)) or b''
    return w, h, decode_tex(data, fmt, siz, w, h, tex.get('tlut'))


# ---------------------------------------------------------------- o2r exporter
class Exporter:
    """Converts display lists from segmented source data into o2r resource files.

    folders: {segnum: o2r folder} for segments whose data is exported alongside (e.g. 6 -> objects/x).
    texinfo: {(seg addr): (fmt, siz, w, h)} known textures (from XML) - otherwise inferred.
    """

    def __init__(self, ctx, folders, filenames=None, names=None, keep=None):
        self.ctx = ctx
        self.keep = keep  # None = keep every triangle
        self.folders = folders
        self.filenames = filenames or {}   # segnum -> file name for auto names (fileVtx_00xxxx)
        self.names = names or {}           # seg addr -> declared name
        self.files = {}
        self.vtx_ranges = {}               # segnum -> list of [start, end)
        self.textures = {}                 # seg addr -> dict(fmt,siz,w,h,bytes)

    # -- pass 1: collect vertex ranges and textures used by a DL tree
    def scan(self, addr, seen=None):
        seen = seen if seen is not None else set()
        if addr in seen:
            return
        seen.add(addr)
        pc = addr
        timg = None
        tiles = {}
        loads = {}
        while True:
            cmd = self.ctx.read(pc, 8)
            if not cmd or len(cmd) < 8:
                break
            w0, w1 = struct.unpack('>II', cmd)
            op = w0 >> 24
            pc += 8
            seg = w1 >> 24
            if op == G_VTX and seg in self.folders:
                n = (w0 >> 12) & 0xFF
                r = self.vtx_ranges.setdefault(seg, [])
                s, e = w1 & 0xFFFFFF, (w1 & 0xFFFFFF) + n * 16
                # ZAPD: loads within one DL that touch become one array
                if r and r[-1][2] == addr and r[-1][1] == s:
                    r[-1][1] = e
                else:
                    r.append([s, e, addr])
            elif op == G_DL:
                if seg in self.folders:
                    self.scan(w1, seen)
                if (w0 >> 16) & 0xFF == 1:
                    break
            elif op == G_ENDDL:
                break
            elif op == G_SETTIMG:
                timg = dict(addr=w1, fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3, width=(w0 & 0xFFF) + 1)
            elif op == G_LOADTLUT and timg:
                cnt = ((w1 >> 14) & 0x3FF) + 1
                self._addtex(timg['addr'], 0, 2, cnt, 1, cnt * 2)
            elif op in (G_LOADBLOCK, G_LOADTILE) and timg:
                if op == G_LOADBLOCK:
                    nbytes = ((((w1 >> 12) & 0xFFF) + 1) * BPP[timg['siz']] + 7) // 8
                else:
                    nbytes = ((((w1 & 0xFFF) >> 2) + 1) * timg['width'] * BPP[timg['siz']] + 7) // 8
                t = (w1 >> 24) & 7
                tmem = tiles.get(t, {}).get('tmem', 0)
                loads[tmem] = (timg, nbytes)
                # dims come from the render tile (settile+settilesize at same tmem)
                self._addtex(timg['addr'], timg['fmt'], timg['siz'], None, None, nbytes)
            elif op == G_SETTILE:
                t = (w1 >> 24) & 7
                tiles.setdefault(t, {}).update(fmt=(w0 >> 21) & 7, siz=(w0 >> 19) & 3, tmem=w0 & 0x1FF)
            elif op == G_SETTILESIZE:
                t = (w1 >> 24) & 7
                tl = tiles.get(t, {})
                w = ((((w1 >> 12) & 0xFFF) - ((w0 >> 12) & 0xFFF)) >> 2) + 1
                h = (((w1 & 0xFFF) - (w0 & 0xFFF)) >> 2) + 1
                ld = loads.get(tl.get('tmem', -1))
                if ld and t != 7:
                    ti, nb = ld
                    self._addtex(ti['addr'], tl['fmt'], tl['siz'], w, h, nb, render=True)

    def _addtex(self, addr, fmt, siz, w, h, nbytes, render=False):
        if (addr >> 24) not in self.folders:
            return
        t = self.textures.setdefault(addr, dict(fmt=fmt, siz=siz, w=w, h=h, bytes=nbytes))
        t['bytes'] = max(t['bytes'], nbytes)
        if render or t['w'] is None:
            if w is not None:
                t.update(fmt=fmt, siz=siz, w=w, h=h)

    def name_of(self, addr, kind):
        if addr in self.names:
            return self.names[addr]
        seg = addr >> 24
        base = self.filenames.get(seg, 'seg%X' % seg)
        suffix = {'vtx': 'Vtx', 'tex': 'Tex', 'dl': 'DL'}[kind]
        return '%s%s_%06X' % (base, suffix, addr & 0xFFFFFF)

    def path(self, addr, kind):
        return '%s/%s' % (self.folders[addr >> 24], self.name_of(addr, kind))

    def _vtx_decl(self, addr):
        seg = addr >> 24
        off = addr & 0xFFFFFF
        for s, e in self.merged[seg]:
            if s <= off < e:
                return (seg << 24) | s, e - s
        raise KeyError(hex(addr))

    # -- pass 2: write resources
    def export(self, dl_addrs, dl_names=None, tri_keep=None):
        """dl_names: {addr: name}. tri_keep(addr_of_dl, tri_cmd_index_list) unused for now."""
        dl_names = dl_names or {}
        self.names.update(dl_names)
        for a in dl_addrs:
            self.scan(a)
        self.merged = {}
        for seg, rs in self.vtx_ranges.items():
            rs = sorted(rs)
            m = []
            for s, e, _ in rs:
                if m and s < m[-1][1]:
                    m[-1][1] = max(m[-1][1], e)
                else:
                    m.append([s, e])
            self.merged[seg] = m
        done = set()
        for a in dl_addrs:
            self._export_dl(a, done)
        for seg, m in self.merged.items():
            for s, e in m:
                a = (seg << 24) | s
                raw = self.ctx.read(a, e - s)
                out = bytearray(header(RT_ARRAY) + struct.pack('<II', 0x19, (e - s) // 16))
                for i in range(0, e - s, 16):
                    x, y, z, f, s_, t, r, g, b, al = struct.unpack_from('>hhhHhhBBBB', raw, i)
                    out += struct.pack('<hhhhhhBBBB', x, y, z, 0, s_, t, r, g, b, al)
                self.files[self.path(a, 'vtx')] = bytes(out)
        for a, ov in getattr(self, 'texinfo', {}).items():
            if a in self.textures:
                self.textures[a].update(ov)
        for a, t in self.textures.items():
            w, h = t['w'], t['h']
            if w is None:
                w, h = t['bytes'] * 8 // BPP[t['siz']], 1
            nb = max((w * h * BPP[t['siz']] + 7) // 8, t['bytes'])
            data = self.ctx.read(a, nb)
            ty = tex_type(t['fmt'], t['siz'])
            self.files[self.path(a, 'tex')] = header(RT_TEXTURE) + struct.pack('<IIII', ty, w, h, len(data)) + data
        return self.files

    def _export_dl(self, addr, done):
        if addr in done:
            return
        done.add(addr)
        p = self.path(addr, 'dl')
        out = bytearray(header(RT_DL))
        out += b'\x04' + b'\xff' * 7
        h = crc64(p)
        out += struct.pack('<IIII', G_MARKER << 24, 0xBEEFBEEF, h >> 32, h & 0xFFFFFFFF)
        pc = addr
        last = None
        children = []
        while True:
            cmd = self.ctx.read(pc, 8)
            if not cmd or len(cmd) < 8:
                break
            w0, w1 = struct.unpack('>II', cmd)
            op = w0 >> 24
            pc += 8
            seg = w1 >> 24
            known = seg in self.folders
            extra = None
            if op == G_VTX:
                if known:
                    decl, _ = self._vtx_decl(w1)
                    diff = (w1 & 0xFFFFFF) - (decl & 0xFFFFFF)
                    w0 = (G_VTX_OTR_HASH << 24) | (w0 & 0xFFFFFF)
                    w1 = diff
                    extra = crc64(self.path(decl, 'vtx'))
                else:
                    w1 = w1 + 1
            elif op == G_DL:
                if known:
                    w0, w1 = G_DL_OTR_HASH << 24, 0
                    extra = crc64(self.path(cmd_w1(cmd), 'dl'))
                    children.append(cmd_w1(cmd))
                else:
                    pp = (w0 >> 16) & 0xFF
                    off = w1 & 0xFFFFFF
                    if off == 0:
                        w1 = (w1 & 0x0FFFFFFF) + 1
                        w0 = (G_DL << 24) | (pp << 16)
                    else:
                        w1 = (seg << 24) | (off // 8)
                        w0 = (G_DL_INDEX << 24) | (pp << 16)
            elif op == G_SETTIMG:
                if known:
                    w0 = (G_SETTIMG_OTR_HASH << 24) | (w0 & 0xFFFFFF)
                    extra = crc64(self.path(w1, 'tex'))
                    w1 = 0
                else:
                    w1 = (w1 & 0x0FFFFFFF) + 1
                    out += struct.pack('<II', w0, w1)   # OTRExporter writes unresolved SETTIMG twice
            elif op in (G_TRI1, G_TRI2) and self.keep is not None:
                here = pc - 8
                k0 = (here, 0) in self.keep
                k1 = op == G_TRI2 and (here, 1) in self.keep
                if not k0 and not k1:
                    continue
                if op == G_TRI2 and not (k0 and k1):
                    if k0:
                        w0 = (G_TRI1 << 24) | (w0 & 0xFFFFFF)
                    else:
                        w0 = (G_TRI1 << 24) | (w1 & 0xFFFFFF)
                    w1 = 0
            elif op == G_SETOTHERMODE_H:
                ss, nn = (w0 >> 8) & 0xFF, w0 & 0xFF
                if 32 - (nn + 1) - ss == 14:   # G_MDSFT_TEXTLUT: OTRExporter writes gsDPSetTextureLUT(dd >> 14)
                    w1 = w1 >> 14
            elif op == G_MTX and not known:
                w1 = (w1 & 0x0FFFFFFF) + 1
            elif op in (G_RDPHALF_1, G_BRANCH_Z):
                raise NotImplementedError('branch-z DLs not supported yet')
            out += struct.pack('<II', w0, w1)
            if extra is not None:
                out += struct.pack('<II', extra >> 32, extra & 0xFFFFFFFF)
            last = op
            if op == G_ENDDL or (op in (G_DL, G_DL_OTR_HASH) and (struct.unpack('>I', cmd[:4])[0] >> 16) & 0xFF == 1):
                break
        if last != G_ENDDL:
            out += struct.pack('<II', G_ENDDL << 24, 0)
        self.files[p] = bytes(out)
        for c in children:
            self._export_dl(c, done)


G_DL_INDEX = 0x3D


def cmd_w1(cmd):
    return struct.unpack('>I', cmd[4:8])[0]

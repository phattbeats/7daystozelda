"""Scouting helpers: load MM objects / rooms, render triangles to PNG (z-buffered, textured)."""
import os, re, sys, math
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, '/tmp/paperclip-run-pha-3945-13a8ec75-d1f-ymLpDk/repo/art/mm-pack')
import z64dl, build_mm_pack as B

ROM = open('/tmp/mm/mm.z64', 'rb').read()
DEC = '/tmp/mm/zeldaret-mm'
FILES = B.rom_files(ROM, open(DEC + '/spec/spec').read())
XML = DEC + '/assets/xml'


def obj_ctx(obj):
    return z64dl.Ctx({6: FILES[obj]})


def obj_dl(obj, name):
    x = open(os.path.join(XML, 'objects', obj + '.xml')).read()
    return 0x06000000 | int(re.search(r'Name="%s" Offset="0x([0-9A-Fa-f]+)"' % name, x).group(1), 16)


def scene_ctx(scene, room):
    x = open(os.path.join(XML, 'scenes', scene, scene + '.xml')).read()
    segs = {2: FILES[scene], 3: FILES['%s_room_%02d' % (scene, room)]}
    tex = re.search(r'misc/(scene_texture_0\d)\.xml', x)
    if tex:
        segs[6] = FILES[tex.group(1)]
    return z64dl.Ctx(segs)


def room_tris(scene, room):
    ctx = scene_ctx(scene, room)
    out = []
    for d in B.room_dls(ctx.segs[3]):
        for t in z64dl.walk(ctx, d):
            t['dl'] = d
            out.append(t)
    return ctx, out


_texcache = {}


def tex_img(ctx, tex):
    key = (id(ctx), tex['src']['addr'], tex['fmt'], tex['siz'], tex.get('w'), tex.get('h'), tuple(tex.get('tlut') or ())[:4])
    if key not in _texcache:
        try:
            w, h, px = z64dl.tri_texture_pixels(ctx, tex)
            a = np.array(px, dtype=np.float32).reshape(h, w, 4) / 255.0
        except Exception:
            a = None
        _texcache[key] = a
    return _texcache[key]


def bbox(tris):
    p = np.array([v[:3] for t in tris for v in t['v']], dtype=float)
    return p.min(0), p.max(0)


def render(ctx, tris, path, yaw=30, pitch=25, size=512, flip=False, light=True, bg=(40, 44, 56)):
    """Orthographic render looking at the bbox centre from yaw/pitch (degrees)."""
    lo, hi = bbox(tris)
    c = (lo + hi) / 2
    r = np.linalg.norm(hi - lo) / 2 + 1e-6
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    # camera basis: right, up, forward(into screen)
    fwd = np.array([-sy * cp, -sp, -cy * cp])
    right = np.array([cy, 0, -sy])
    up = np.cross(right, fwd)
    img = np.zeros((size, size, 3), dtype=np.float32); img[:] = np.array(bg) / 255
    zb = np.full((size, size), -1e9, dtype=np.float32)
    sc = size / (2.2 * r)
    for t in tris:
        P = np.array([v[:3] for v in t['v']], dtype=float) - c
        X = P @ right * sc + size / 2
        Y = size / 2 - P @ up * sc
        Z = -(P @ fwd)  # larger = closer
        n = np.cross(P[1] - P[0], P[2] - P[0]); nn = np.linalg.norm(n)
        shade = 1.0
        if light and nn > 0:
            shade = 0.55 + 0.45 * abs(np.dot(n / nn, np.array([0.3, 0.8, 0.5]) / np.linalg.norm([0.3, 0.8, 0.5])))
        x0, x1 = int(max(0, math.floor(X.min()))), int(min(size - 1, math.ceil(X.max())))
        y0, y1 = int(max(0, math.floor(Y.min()))), int(min(size - 1, math.ceil(Y.max())))
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0])
        if abs(d) < 1e-9:
            continue
        w1 = ((xs - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (ys - Y[0])) / d
        w2 = ((X[1] - X[0]) * (ys - Y[0]) - (xs - X[0]) * (Y[1] - Y[0])) / d
        w0 = 1 - w1 - w2
        m = (w0 >= -1e-4) & (w1 >= -1e-4) & (w2 >= -1e-4)
        if not m.any():
            continue
        z = w0 * Z[0] + w1 * Z[1] + w2 * Z[2]
        sub = zb[y0:y1 + 1, x0:x1 + 1]
        m &= z > sub
        if not m.any():
            continue
        vc = np.array([v[5:9] for v in t['v']], dtype=float) / 255
        if t['lit']:
            vc[:, :3] = 1.0
        col = (w0[..., None] * vc[0] + w1[..., None] * vc[1] + w2[..., None] * vc[2])[..., :3]
        prim = np.array(t['prim'][:3]) / 255
        tex = t['tex']
        if tex is not None:
            a = tex_img(ctx, tex)
            if a is not None:
                h, w = a.shape[:2]
                S = np.array([v[3] for v in t['v']], dtype=float) * tex.get('sscale', 1) / 32 - tex.get('uls', 0) / 4
                T = np.array([v[4] for v in t['v']], dtype=float) * tex.get('tscale', 1) / 32 - tex.get('ult', 0) / 4
                s = (w0 * S[0] + w1 * S[1] + w2 * S[2]).astype(int) % w
                tt = (w0 * T[0] + w1 * T[1] + w2 * T[2]).astype(int) % h
                tc = a[tt, s]
                alpha = tc[..., 3]
                m &= alpha > 0.3
                col = col * tc[..., :3]
        col = np.clip(col * prim * shade, 0, 1)
        sub[m] = z[m]
        img[y0:y1 + 1, x0:x1 + 1][m] = col[m]
    im = Image.fromarray((img * 255).astype(np.uint8))
    if path:
        im.save(path)
    return im


def sheet(items, path, cols=4, cell=384):
    """items: [(label, PIL image)]"""
    rows = (len(items) + cols - 1) // cols
    out = Image.new('RGB', (cols * cell, rows * (cell + 20)), (20, 20, 24))
    d = ImageDraw.Draw(out)
    for i, (lab, im) in enumerate(items):
        x, y = (i % cols) * cell, (i // cols) * (cell + 20)
        out.paste(im.resize((cell, cell)), (x, y + 20))
        d.text((x + 4, y + 4), lab, fill=(255, 220, 120))
    out.save(path)

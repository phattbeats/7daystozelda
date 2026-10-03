"""Build the 7 Days to Zelda Majora's Mask model pack (PHA-3904).

Cuts a few models out of a Majora's Mask (USA) ROM and writes them as libultraship
o2r resources under objects/7dtz_mm/, the paths Placeables.cpp draws:

  objects/7dtz_mm/maruta/gMMPracticeLogDL  palisade stake (Swordsman's School practice log)
  objects/7dtz_mm/kgy/gMMSmithyHammerDL    workbench: Gabora's smithing hammer
  objects/7dtz_mm/kgy/gMMSmithyBladeDL     workbench: a red-hot sword blank
  objects/7dtz_mm/gMMInnDeskDL             workbench: the Stock Pot Inn's desk (room geometry)

The output is Nintendo data: never commit it or attach it to a public release. It
ships only inside the server's soh.o2r (--o2r appends it there).

Usage:
  python3 build_mm_pack.py --rom mm.z64 --decomp path/to/zeldaret-mm --out pack/ [--o2r soh.o2r]

--decomp needs a checkout of https://github.com/zeldaret/mm with spec/spec and
assets/xml (a sparse checkout of those two is enough). Requires crunch64 (pip).
"""
import argparse
import hashlib
import os
import re
import struct
import zipfile

import crunch64

import z64dl

MM_USA_SHA1 = "d6133ace5afaa0882cf214cf88daba39e266c078"
DMADATA = 0x1A500


def rom_files(rom, spec):
    """Decompressed files of the ROM by spec segment name."""
    names = []
    for m in re.finditer(r"beginseg(.*?)endseg", spec, re.S):
        body = m.group(1)
        name = re.search(r'name\s+"([^"]+)"', body).group(1)
        # NOLOAD segments have no dmadata entry; Bg_Heavy_Block is Japanese-only.
        if re.search(r"flags\s+.*NOLOAD", body) or name == "ovl_Bg_Heavy_Block":
            continue
        names.append(name)
    files, off, i = {}, DMADATA, 0
    while True:
        vs, ve, ps, pe = struct.unpack(">4I", rom[off:off + 16])
        if vs == 0 and ve == 0 and ps == 0:
            break
        if ps != 0xFFFFFFFF:
            data = rom[ps:ps + ve - vs] if pe == 0 else crunch64.yaz0.decompress(rom[ps:pe])
            files[names[i]] = data
        off += 16
        i += 1
    return files


def room_dls(room):
    """Opaque and translucent display lists of a room's mesh (shape types 0 and 2)."""
    out, pc = [], 0
    while pc < len(room):
        cmd, w1 = room[pc], struct.unpack(">I", room[pc + 4:pc + 8])[0]
        pc += 8
        if cmd == 0x14:
            break
        if cmd == 0x0A:
            mh = w1 & 0xFFFFFF
            if room[mh] in (0, 2):
                count, start = room[mh + 1], struct.unpack(">I", room[mh + 4:mh + 8])[0] & 0xFFFFFF
                size = 8 if room[mh] == 0 else 16
                for e in range(start, start + count * size, size):
                    out += [d for d in struct.unpack(">II", room[e + size - 8:e + size]) if d]
    return out


def bbox(tris):
    pts = [v[:3] for t in tris for v in t["v"]]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def wrapper(path, children):
    """A display list that just calls others (one per room mesh list)."""
    out = bytearray(z64dl.header(z64dl.RT_DL)) + b"\x04" + b"\xff" * 7
    h = z64dl.crc64(path)
    out += struct.pack("<IIII", z64dl.G_MARKER << 24, 0xBEEFBEEF, h >> 32, h & 0xFFFFFFFF)
    for c in children:
        hc = z64dl.crc64(c)
        out += struct.pack("<IIII", z64dl.G_DL_OTR_HASH << 24, 0, hc >> 32, hc & 0xFFFFFFFF)
    out += struct.pack("<II", z64dl.G_ENDDL << 24, 0)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom", required=True)
    ap.add_argument("--decomp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--o2r", help="append the pack to this soh.o2r")
    a = ap.parse_args()

    rom = open(a.rom, "rb").read()
    if hashlib.sha1(rom).hexdigest() != MM_USA_SHA1:
        raise SystemExit("not a Majora's Mask (USA) ROM: SHA-1 must be " + MM_USA_SHA1)
    files = rom_files(rom, open(os.path.join(a.decomp, "spec", "spec")).read())
    xml_dir = os.path.join(a.decomp, "assets", "xml")
    out = {}

    def object_dl(obj, name):
        x = open(os.path.join(xml_dir, "objects", obj + ".xml")).read()
        return 0x06000000 | int(re.search(r'Name="%s" Offset="0x([0-9A-Fa-f]+)"' % name, x).group(1), 16)

    def export_object(obj, addr, outname):
        folder = "objects/7dtz_mm/" + obj[7:]
        ctx = z64dl.Ctx({6: files[obj]})
        ex = z64dl.Exporter(ctx, {6: folder}, {6: obj[7:]}, names={addr: outname})
        out.update(ex.export([addr]))
        print(outname, "bbox", bbox(z64dl.walk(ctx, addr)))

    def scene_ctx(scene, room):
        x = open(os.path.join(xml_dir, "scenes", scene, scene + ".xml")).read()
        segs = {2: files[scene], 3: files["%s_room_%02d" % (scene, room)]}
        tex = re.search(r"misc/(scene_texture_0\d)\.xml", x)
        if tex:
            segs[6] = files[tex.group(1)]
        return z64dl.Ctx(segs)

    def export_room_box(scene, room, lo, hi, outname):
        """Every triangle of the room inside the box lo..hi (room coordinates)."""
        ctx = scene_ctx(scene, room)
        base = "objects/7dtz_mm/%s%d" % (scene[3:].lower(), room)
        folders = {2: base + "/scene", 3: base + "/room"}
        if 6 in ctx.segs:
            folders[6] = base + "/scenetex"
        tops, keep, kept = [], set(), []
        for d in room_dls(ctx.segs[3]):
            inside = [t for t in z64dl.walk(ctx, d)
                      if all(lo[i] <= v[i] <= hi[i] for v in t["v"] for i in range(3))]
            if inside:
                tops.append(d)
                keep |= {t["src"] for t in inside}
                kept += inside
        ex = z64dl.Exporter(ctx, folders, {s: f.split("/")[-1] for s, f in folders.items()}, keep=keep)
        out.update(ex.export(tops))
        path = "objects/7dtz_mm/" + outname
        out[path] = wrapper(path, [ex.path(d, "dl") for d in tops])
        print(outname, len(kept), "tris, bbox", bbox(kept))

    export_object("object_maruta", object_dl("object_maruta", "gPracticeLogWholeDL"), "gMMPracticeLogDL")
    export_object("object_kgy", 0x0600A1C0, "gMMSmithyHammerDL")  # En_Kgy's hammer limb
    export_object("object_kgy", 0x0600E8F0, "gMMSmithyBladeDL")   # the red-hot blade; calls segs 8/9
    export_room_box("Z2_YADOYA", 3, (-435, 210, 360), (-391, 239, 389), "gMMInnDeskDL")

    for path, data in out.items():
        fp = os.path.join(a.out, path)
        os.makedirs(os.path.dirname(fp), exist_ok=True)
        with open(fp, "wb") as f:
            f.write(data)
    print(len(out), "resources,", sum(map(len, out.values())), "bytes in", a.out)

    if a.o2r:
        with zipfile.ZipFile(a.o2r, "a", compression=zipfile.ZIP_DEFLATED) as z:
            have = set(z.namelist())
            for path, data in out.items():
                if path in have:
                    raise SystemExit(a.o2r + " already has the pack (" + path + "): start from a clean copy")
                z.writestr(path, data)
        print("appended to", a.o2r)


if __name__ == "__main__":
    main()

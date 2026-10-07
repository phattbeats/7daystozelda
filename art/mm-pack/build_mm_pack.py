"""Build the 7 Days to Zelda Majora's Mask model pack (#3904).

Cuts a few models out of a Majora's Mask (USA) ROM and writes them as libultraship
o2r resources under objects/7dtz_mm/, the paths Placeables.cpp draws:

  objects/7dtz_mm/maruta/gMMPracticeLogDL  palisade stake (Swordsman's School practice log)
  objects/7dtz_mm/kgy/gMMSmithyHammerDL    workbench: Gabora's smithing hammer
  objects/7dtz_mm/kgy/gMMSmithyBladeDL     workbench: a red-hot sword blank
  objects/7dtz_mm/gMMInnDeskDL             workbench: the Stock Pot Inn's desk (room geometry)

#3945, floors, stairs and doors:
  objects/7dtz_mm/taru/gMMPiratePanelDL       plank floor, wooden step: Pirates' Fortress breakable panel
  objects/7dtz_mm/gMMRanchPlankDL             ranch floor: a plank from the Romani Ranch house (room geometry)
  objects/7dtz_mm/raillift/gMMStonePlatformDL stone platform: a Woodfall Temple moving platform
  objects/7dtz_mm/tokei_turret/gMMFestivalDeckDL festival deck: the top of the Clock Town carnival tower
  objects/7dtz_mm/ladder/gMMLadderDL          ladder: the 12-rung wooden ladder; calls segment 0x0C
  objects/7dtz_mm/gMMInnStairsDL              staircase: the Stock Pot Inn's lobby stairs (room geometry)
  objects/7dtz_mm/dor03/gMMSwampDoorDL        doors: the Southern Swamp door,
  objects/7dtz_mm/kaizoku_obj/gMMPirateDoorDL   the Pirates' Fortress door
  objects/7dtz_mm/wdor05/gMMMusicBoxDoorDL      and the Music Box House door

#3962, furniture (room pieces are cut from the room meshes like the inn desk):
  objects/7dtz_mm/gMMInnChairDL                 Stock Pot Inn chair (room 2)
  objects/7dtz_mm/gMMInnBenchDL                 the inn lobby's long bench (room 0)
  objects/7dtz_mm/mbar_obj/gMMMilkBarChairDL    Milk Bar chair
  objects/7dtz_mm/gMMInnBedDL                   Stock Pot Inn bed (room 2)
  objects/7dtz_mm/gMMMayorBedDL                 the Mayor's bed (Mayor's Residence room 3)
  objects/7dtz_mm/gMMInnDresserDL               Stock Pot Inn dresser (room 2)
  objects/7dtz_mm/kin2_obj/gMMDrawersDL         Oceanside Spider House chest of drawers,
  objects/7dtz_mm/kin2_obj/gMMBookshelfDL         bookshelf
  objects/7dtz_mm/kin2_obj/gMMPaintingDL          and Skull Kid painting
  objects/7dtz_mm/gMMMilkCanDL                  Romani Ranch milk can (house room 1)
  objects/7dtz_mm/gMMRugDL                      the pink rug on the ranch house wall (room 2)
  objects/7dtz_mm/taru/gMMBarrelDL              barrel
  objects/7dtz_mm/gMMRomaniBarrelDL             Romani Ranch barrel (house room 0)
  objects/7dtz_mm/gMMWagonWheelDL               wagon wheel (house room 0)
  objects/7dtz_mm/tokei_turret/gMMFestivalStallDL the carnival tower's cloth-walled base

and Majora's Mask player animations that OoT doesn't have. MM's Link has OoT's
skeleton (22 limbs, same order) and the same frame layout (67 s16 per frame), so they
play on OoT's Link as they are:
  objects/7dtz_mm/anim/gPlayerAnim_<name>       Link animation header (frame count + data path)
  objects/7dtz_mm/anim/gPlayerAnim_<name>_Data  the frames
  demo_suwari1..3: sitting with his legs hanging; sirimochi(_wait): sitting on the floor;
  okiagaru, okiagaru_wait, okiagaru_tatu: lying down, sitting up, getting up.

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
RT_ANIMATION, RT_PLAYER_ANIMATION = 0x4F414E4D, 0x4F50414D  # SOH_Animation, SOH_PlayerAnimation
ANIM_TYPE_LINK = 1
PLAYER_ANIM_FRAME = 67  # s16 per frame: 22 limbs x 3 rotations + the face


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

    room_cuts = set()

    def export_room_box(scene, room, lo, hi, outname, only=None):
        """Every triangle of the room inside the box lo..hi (room coordinates), from the
        room's display lists in `only` when it is given."""
        ctx = scene_ctx(scene, room)
        base = "objects/7dtz_mm/%s%d" % (scene[3:].lower(), room)
        # Each cut keeps different triangles of the same room lists: a second cut from a
        # room gets its own folder, or it overwrites the first one's lists (#3962).
        room_folder = base + "/room" if base not in room_cuts else base + "/room_" + outname
        room_cuts.add(base)
        folders = {2: base + "/scene", 3: room_folder}
        if 6 in ctx.segs:
            folders[6] = base + "/scenetex"
        tops, keep, kept = [], set(), []
        for d in room_dls(ctx.segs[3]):
            if only is not None and d & 0xFFFFFF not in only:
                continue
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
    # #3945: floors, stairs and doors.
    export_object("object_taru", object_dl("object_taru", "gObjTaruBreakablePiratePanelDL"), "gMMPiratePanelDL")
    export_room_box("Z2_OMOYA", 1, (600, 57, -100), (640, 63, 64), "gMMRanchPlankDL")
    export_object("object_raillift", 0x06001E40, "gMMStonePlatformDL")
    export_object("object_tokei_turret", object_dl("object_tokei_turret", "gClockTownTurretPlatformTopDL"),
                  "gMMFestivalDeckDL")
    export_object("object_ladder", object_dl("object_ladder", "gWoodenLadder12RungDL"), "gMMLadderDL")
    # The lobby stairs: the tread ramp, the side panel under it and the banister, not the
    # counter that stands in the same box.
    export_room_box("Z2_YADOYA", 0, (-30, 0, -150), (215, 249, -23), "gMMInnStairsDL",
                    only={0x000BE8, 0x008BD8, 0x00A948})
    export_object("object_dor03", object_dl("object_dor03", "gSwampDoorDL"), "gMMSwampDoorDL")
    export_object("object_kaizoku_obj", object_dl("object_kaizoku_obj", "gPiratesFortressDoorDL"), "gMMPirateDoorDL")
    export_object("object_wdor05", object_dl("object_wdor05", "gMusicBoxHouseDoorDL"), "gMMMusicBoxDoorDL")

    # #3962: furniture. Room boxes are each piece's bounds, 1 unit wider.
    export_room_box("Z2_YADOYA", 2, (-433, 209, -70), (-406, 255, -44), "gMMInnChairDL")
    export_room_box("Z2_YADOYA", 0, (284, -1, 119), (321, 31, 271), "gMMInnBenchDL")
    export_object("object_mbar_obj", 0x06000288, "gMMMilkBarChairDL")
    export_room_box("Z2_YADOYA", 2, (-592, 209, -237), (-518, 235, -127), "gMMInnBedDL")
    export_room_box("Z2_SONCHONOIE", 3, (569, -1, -52), (676, 37, 22), "gMMMayorBedDL")
    export_room_box("Z2_YADOYA", 2, (-466, 209, -241), (-434, 256, -224), "gMMInnDresserDL")
    export_object("object_kin2_obj", object_dl("object_kin2_obj", "gOceanSpiderHouseChestOfDrawersDL"), "gMMDrawersDL")
    export_object("object_kin2_obj", object_dl("object_kin2_obj", "gOceanSpiderHouseBookshelfDL"), "gMMBookshelfDL")
    export_object("object_kin2_obj", object_dl("object_kin2_obj", "gOceanSpiderHouseSkullkidPaintingDL"),
                  "gMMPaintingDL")
    export_room_box("Z2_OMOYA", 1, (1098, -1, -181), (1139, 54, -145), "gMMMilkCanDL")
    export_room_box("Z2_OMOYA", 2, (783, 307, -111), (790, 332, -86), "gMMRugDL")
    export_object("object_taru", object_dl("object_taru", "gObjTaruBarrelDL"), "gMMBarrelDL")
    export_room_box("Z2_OMOYA", 0, (-393, -1, -199), (-334, 41, -148), "gMMRomaniBarrelDL")
    export_room_box("Z2_OMOYA", 0, (-66, 14, 89), (1, 81, 99), "gMMWagonWheelDL")
    export_object("object_tokei_turret", object_dl("object_tokei_turret", "gClockTownTurretPlatformBaseDL"),
                  "gMMFestivalStallDL")

    # #3962: Majora's Mask Link animations (gameplay_keep's headers point into
    # link_animetion; the frames are big-endian s16 in the ROM, little-endian in o2r).
    anim_xml = open(os.path.join(xml_dir, "misc", "link_animetion.xml")).read()

    def export_player_anim(name):
        m = re.search(r'Name="gPlayerAnim_%s_Data" FrameCount="(\d+)" Offset="0x([0-9A-Fa-f]+)"' % name, anim_xml)
        frames, off = int(m.group(1)), int(m.group(2), 16)
        n = frames * PLAYER_ANIM_FRAME
        values = struct.unpack(">%dh" % n, files["link_animetion"][off:off + n * 2])
        data_path = "objects/7dtz_mm/anim/gPlayerAnim_%s_Data" % name
        out[data_path] = z64dl.header(RT_PLAYER_ANIMATION) + struct.pack("<I%dh" % n, n, *values)
        ref = ("__OTR__" + data_path).encode()
        out["objects/7dtz_mm/anim/gPlayerAnim_" + name] = (z64dl.header(RT_ANIMATION) +
                                                           struct.pack("<IHI", ANIM_TYPE_LINK, frames, len(ref)) + ref)
        print("gPlayerAnim_" + name, frames, "frames")

    for name in ("demo_suwari1", "demo_suwari2", "demo_suwari3", "sirimochi", "sirimochi_wait", "okiagaru",
                 "okiagaru_wait", "okiagaru_tatu"):
        export_player_anim(name)

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

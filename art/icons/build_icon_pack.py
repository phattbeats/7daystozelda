#!/usr/bin/env python3
"""Pack the Workbench kit icons into soh.o2r (PHA-3969).

The icons are the pieces' own models, rendered in-game by the icon studio
(tools/harness/pha3969/capture.py) and cut out by tools/harness/pha3969/matte.py:
32x32 RGBA PNGs named after the kit ids (workbench.png, barricade.png, ...,
packup.png, packall.png). This writes each one as an RGBA32 texture resource at
objects/7dtz_icons/<name>, which CraftingWindow.cpp points the rows at.

    python3 build_icon_pack.py --icons icons/ --o2r soh.o2r

The renders are of Nintendo models: like the MM pack, never commit them or attach
them to a public release. To update a live soh.o2r, copy it without its
objects/7dtz_icons/ entries first (--o2r refuses an archive that has them).
"""
import argparse
import os
import struct
import sys
import zipfile

from PIL import Image

RT_TEXTURE = 0x4F544558
TEX_RGBA32 = 1
NAMES = ["workbench", "barricade", "spikes", "chest", "scarecrow", "guardbaba", "torch", "stonewall",
         "bombtrap", "gate", "ironwall", "palisade", "floorplank", "floorranch", "floorstone", "deck",
         "step", "ladder", "stairs", "doorswamp", "doormusic", "doorpirate", "packup", "packall"]


def header(restype):
    h = struct.pack("<BBBBIIQIQI", 0, 0, 0, 0, restype, 0, 0xDEADBEEFDEADBEEF, 0, 0, 0)
    return h + b"\0" * (0x40 - len(h))


def texture(png):
    im = Image.open(png).convert("RGBA")
    if im.size != (32, 32):
        raise SystemExit(f"{png}: {im.size}, want 32x32")
    data = im.tobytes()  # R, G, B, A per pixel: the N64's RGBA32 byte order
    return header(RT_TEXTURE) + struct.pack("<IIII", TEX_RGBA32, 32, 32, len(data)) + data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--icons", required=True)
    ap.add_argument("--o2r", required=True)
    a = ap.parse_args()
    missing = [n for n in NAMES if not os.path.exists(os.path.join(a.icons, n + ".png"))]
    if missing:
        raise SystemExit("missing icons: " + ", ".join(missing))
    with zipfile.ZipFile(a.o2r) as z:
        if any(n.startswith("objects/7dtz_icons/") for n in z.namelist()):
            raise SystemExit(f"{a.o2r} already has objects/7dtz_icons/: start from a copy without them")
    with zipfile.ZipFile(a.o2r, "a", compression=zipfile.ZIP_DEFLATED) as z:
        for n in NAMES:
            z.writestr("objects/7dtz_icons/" + n, texture(os.path.join(a.icons, n + ".png")))
    print(f"added {len(NAMES)} icons to {a.o2r}")


if __name__ == "__main__":
    sys.exit(main())

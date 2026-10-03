"""Build 7 Days to Zelda title-screen texture overrides from the logo art.

Reads the original OTEX headers from oot.o2r and writes replacements for
object_mag: the shield logo (HD, v1 resource), blanked "The Legend of" /
"Ocarina of Time" captions, and flame masks traced from the new logo.
Usage: python3 build_title_pack.py oot.o2r logo.png out_dir
"""
import os
import struct
import sys
import zipfile

from PIL import Image, ImageFilter

oot, logo_path, out_dir = sys.argv[1:4]
z = zipfile.ZipFile(oot)
S = 1  # native 160x160: EnMag streams the logo in row strips, which garbles HD (v1 scaled) textures


def tex(n):
    return z.read("objects/object_mag/" + n)


def hdr(n, ver):
    return tex(n)[:8] + struct.pack("<I", ver) + tex(n)[12:64]


out = {}

src = Image.open(logo_path).convert("RGBA")
src = src.crop(src.getbbox())
W = 160 * S
h = round(src.height * W / src.width)
canvas = Image.new("RGBA", (W, W), (0, 0, 0, 0))
canvas.paste(src.resize((W, h), Image.LANCZOS).filter(ImageFilter.UnsharpMask(1, 60, 2)), (0, (W - h) // 2))
data = canvas.tobytes()
out["gTitleZeldaShieldLogoTex"] = (hdr("gTitleZeldaShieldLogoTex", 0)
                                   + struct.pack("<IIII", 1, W, W, len(data)) + data)

# Captions are I8 overlays on the old logo; zero intensity draws nothing.
for n, (w, hh) in {"gTitleTheLegendOfTextTex": (72, 8), "gTitleOcarinaOfTimeTMTextTex": (96, 8)}.items():
    out[n] = hdr(n, 0) + struct.pack("<IIII", 6, w, hh, w * hh) + bytes(w * hh)

# Flame masks: a 3x3 grid of 64x64 I4 tiles at screen (64,0); the logo sits at (80,20).
a = canvas.split()[3].resize((160, 160), Image.LANCZOS).point(lambda v: 255 if v > 20 else 0)
m = Image.new("L", (192, 192), 0)
m.paste(a, (16, 20))
m = m.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(5))
for k in range(9):
    t = m.crop(((k % 3) * 64, (k // 3) * 64, (k % 3) * 64 + 64, (k // 3) * 64 + 64))
    p = [v >> 4 for v in t.getdata()]
    b = bytes((p[i] << 4) | p[i + 1] for i in range(0, len(p), 2))
    n = "gTitleEffectMask%d%dTex" % (k // 3, k % 3)
    out[n] = hdr(n, 0) + struct.pack("<IIII", 5, 64, 64, len(b)) + b

os.makedirs(out_dir, exist_ok=True)
for n, d in out.items():
    with open(os.path.join(out_dir, n), "wb") as f:
        f.write(d)

prev = Image.new("RGBA", (320, 240), (20, 10, 30, 255))
fl = Image.new("RGBA", (192, 192), (255, 120, 30, 255))
fl.putalpha(m)
prev.paste(fl, (64, 0), fl)
small = canvas.resize((160, 160), Image.LANCZOS)
prev.paste(small, (80, 20), small)
prev.resize((960, 720), Image.NEAREST).save(os.path.join(out_dir, "..", "preview.png"))
print({k: len(v) for k, v in out.items()})

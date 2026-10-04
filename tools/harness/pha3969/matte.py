# Black/white matting: alpha = 1 - (white - black), colour = black / alpha. Then crop to the
# subject, pad to a square and downscale to 32x32 (premultiplied, so edges don't fringe).
import sys, os
import numpy as np
from PIL import Image
raw, out = sys.argv[1], sys.argv[2]
X0, Y0, X1, Y1 = 290, 80, 670, 460    # clear of the hearts, the rupees and textboxes
for f in sorted(os.listdir(raw)):
    if not f.endswith('_b.png'): continue
    name = f[:-6]
    b = np.asarray(Image.open(os.path.join(raw, f)).convert('RGB'), dtype=np.float64)[Y0:Y1, X0:X1] / 255
    w = np.asarray(Image.open(os.path.join(raw, name + '_w.png')).convert('RGB'), dtype=np.float64)[Y0:Y1, X0:X1] / 255
    a = np.clip(1 - (w - b).mean(axis=2), 0, 1)
    a[a < 0.03] = 0
    a[:112 - Y0, 595 - X0:] = 0           # the A/B buttons' corner
    ys, xs = np.nonzero(a)
    edge = ys.min() == 0 or xs.min() == 0 or ys.max() == a.shape[0] - 1 or xs.max() == a.shape[1] - 1
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    side = int(max(y1 - y0, x1 - x0) * 1.06) + 2
    cy, cx = (y0 + y1) // 2, (x0 + x1) // 2
    prem = np.zeros((side, side, 4))
    sy, sx = cy - side // 2, cx - side // 2
    for yy in range(side):
        ry = sy + yy
        if not 0 <= ry < a.shape[0]: continue
        lo, hi = max(sx, 0), min(sx + side, a.shape[1])
        prem[yy, lo - sx:hi - sx, :3] = b[ry, lo:hi]          # already premultiplied (on black)
        prem[yy, lo - sx:hi - sx, 3] = a[ry, lo:hi]
    img = Image.fromarray((prem * 255).round().astype(np.uint8), 'RGBA')
    small = np.asarray(img.resize((32, 32), Image.LANCZOS), dtype=np.float64) / 255
    sa = np.clip(small[..., 3:4], 0, 1)
    rgb = np.where(sa > 0.004, np.clip(small[..., :3] / np.maximum(sa, 1e-6), 0, 1), 0)
    rgb = np.clip(rgb ** 0.85 * 1.08, 0, 1)                  # indoor light is dim: lift it a little
    res = np.concatenate([rgb, sa], axis=2)
    Image.fromarray((res * 255).round().astype(np.uint8), 'RGBA').save(os.path.join(out, name + '.png'))
    print(name, 'bbox', (x0, y0, x1, y1), 'TOUCHES EDGE' if edge else '')

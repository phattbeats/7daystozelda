# PHA-4060 trailer fort v2: radius-600 octagon, gatehouse, 2-storey corner towers (piece list only)
import math
CX, CZ, A_ = 150.0, 2000.0, 600.0
T = dict(barricade=0, spikes=1, workbench=2, chest=3, scarecrow=5, guardbaba=6, torch=7, stonewall=8, bombtrap=9,
         gate=10, palisade=12, deck=16, ladder=18, barrel=35, wagonwheel=37, stall=38)
def P_(d, t, yaw):
    s, c = math.sin(yaw), math.cos(yaw)
    return CX + d * s + t * c, CZ + d * c - t * s
def build():
    out = []; add = lambda k, x, z, r=0, y=0.0: out.append((k, round(x, 1), y, round(z, 1), int(r) & 0xFFFF))
    add('workbench', CX, CZ, 0)
    walls = []
    for i in range(8):
        rot = i * 0x2000; yaw = i * math.pi / 4
        offs = (-186, -122, 0, 122, 186) if i == 0 else (-186, -62, 62, 186)
        for t in offs:
            x, z = P_(A_, t, yaw); walls.append(('gate' if (i == 0 and t == 0) else 'palisade', x, z, rot))
    # build order: walls spiral out from the gate both ways
    order = sorted(range(len(walls)), key=lambda j: min(j, len(walls) - j))
    for j in order: add(*walls[j])
    decks = []
    for i in range(8):
        rot = i * 0x2000; yaw = i * math.pi / 4
        for t in ((-150, 150) if i == 0 else (0,)):
            x, z = P_(A_ - 16 - 62, t, yaw); decks.append((x, z, rot))
    for x, z, r in decks: add('deck', x, z, r)
    towers = []
    for i in (1, 3, 5, 7):
        yaw = i * math.pi / 4 + math.pi / 8; x, z = P_(A_ / math.cos(math.pi / 8) - 110, 0, yaw); towers.append((x, z))
    for x, z in towers: add('deck', x, z, 0)
    for x, z in towers: add('deck', x, z, 0, 104.0)
    for x, z, r in decks: add('torch', x, z, 0, 104.0)
    for x, z in towers: add('torch', x, z, 0, 208.0)
    for t in (-80, 80):
        x, z = P_(A_ + 40, t, 0); add('torch', x, z)
    for i in range(8):
        rot = i * 0x2000; yaw = i * math.pi / 4
        for t in (-150, 150):
            x, z = P_(A_ + 80, t, yaw); add('spikes', x, z, rot)
    for i in (1, 3, 5, 7):
        yaw = i * math.pi / 4; x, z = P_(A_ + 170, 0, yaw); add('bombtrap', x, z)
    for i in (2, 6):
        yaw = i * math.pi / 4; x, z = P_(A_ + 150, 0, yaw); add('guardbaba', x, z, i * 0x2000)
    x, z = P_(780, 0, math.pi * 0.9); add('scarecrow', x, z, 0x8000)
    add('chest', CX - 90, CZ - 80, 0); add('chest', CX + 90, CZ - 80, 0)
    add('barrel', CX - 320, CZ - 200); add('stall', CX - 260, CZ + 230, 0x2000); add('wagonwheel', CX + 300, CZ + 220, 0x6000)
    for t in (-150, 150):
        x, z = P_(A_ - 16 - 62 - 64, t, 0); add('ladder', x, z, 0x8000)
    return out
if __name__ == '__main__':
    b = build(); print(len(b))
    from collections import Counter; print(Counter(k for k, *_ in b))

# Majora's Mask model pack

`build_mm_pack.py` cuts the few Majora's Mask models 7 Days to Zelda uses out of a Majora's Mask (USA) ROM and writes them as o2r resources under `objects/7dtz_mm/`. `Placeables.cpp` draws them when they are present and falls back to OoT models when they are not.

| Resource | Used for | From |
|---|---|---|
| `maruta/gMMPracticeLogDL` | Palisade wall (five logs, 96 tall) | Swordsman's School practice log |
| `kgy/gMMSmithyHammerDL` | Workbench | Gabora's hammer, Mountain Village smithy |
| `kgy/gMMSmithyBladeDL` | Workbench | Red-hot sword blank, same object |
| `gMMInnDeskDL` | Workbench (scaled 1.8x) | Stock Pot Inn room 3 desk, cut from the room mesh |

```
python3 build_mm_pack.py --rom mm.z64 --decomp zeldaret-mm --out pack/ --o2r soh.o2r
```

`--decomp` is a checkout of [zeldaret/mm](https://github.com/zeldaret/mm); only `spec/spec` and `assets/xml/` are read. The pack is Nintendo data: never commit it or attach it to a public release.

`z64dl.py` walks F3DEX2 display lists and converts them the way SoH's `OTRExporter/DisplayListExporter.cpp` does. Its output is byte-identical to `oot.o2r` for OoT objects whose textures and vertices are declared the same way, which is how it was checked.

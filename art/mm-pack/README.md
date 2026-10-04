# Majora's Mask model pack

`build_mm_pack.py` cuts the few Majora's Mask models 7 Days to Zelda uses out of a Majora's Mask (USA) ROM and writes them as o2r resources under `objects/7dtz_mm/`. `Placeables.cpp` draws them when they are present and falls back to OoT models when they are not.

| Resource | Used for | From |
|---|---|---|
| `maruta/gMMPracticeLogDL` | Palisade wall (five logs, 96 tall) | Swordsman's School practice log |
| `kgy/gMMSmithyHammerDL` | Workbench | Gabora's hammer, Mountain Village smithy |
| `kgy/gMMSmithyBladeDL` | Workbench | Red-hot sword blank, same object |
| `gMMInnDeskDL` | Workbench (scaled 1.8x) | Stock Pot Inn room 3 desk, cut from the room mesh |
| `taru/gMMPiratePanelDL` | Plank floor, wooden step (PHA-3945) | Pirates' Fortress breakable panel (object_taru) |
| `gMMRanchPlankDL` | Ranch floor (three planks) | A plank in the Romani Ranch house, room 1, cut from the room mesh |
| `raillift/gMMStonePlatformDL` | Stone platform | Woodfall Temple moving platform (object_raillift) |
| `tokei_turret/gMMFestivalDeckDL` | Festival deck | Top of the Clock Town carnival tower (object_tokei_turret) |
| `ladder/gMMLadderDL` | Ladder | 12-rung wooden ladder (object_ladder); calls segment 0x0C |
| `gMMInnStairsDL` | Inn staircase | Stock Pot Inn lobby stairs (room 0): ramp, side panel and banister |
| `dor03/gMMSwampDoorDL` | Swamp door | Southern Swamp door (object_dor03) |
| `wdor05/gMMMusicBoxDoorDL` | Music Box House door | Music Box House door (object_wdor05) |
| `kaizoku_obj/gMMPirateDoorDL` | Pirates' Fortress door | Pirates' Fortress door (object_kaizoku_obj) |

```
python3 build_mm_pack.py --rom mm.z64 --decomp zeldaret-mm --out pack/ --o2r soh.o2r
```

`--decomp` is a checkout of [zeldaret/mm](https://github.com/zeldaret/mm); only `spec/spec` and `assets/xml/` are read. The pack is Nintendo data: never commit it or attach it to a public release. `--o2r` refuses an archive that already has the pack, so to update a live `soh.o2r`, copy it without its `objects/7dtz_mm/` entries first.

`z64dl.py` walks F3DEX2 display lists and converts them the way SoH's `OTRExporter/DisplayListExporter.cpp` does. Its output is byte-identical to `oot.o2r` for OoT objects whose textures and vertices are declared the same way, which is how it was checked.

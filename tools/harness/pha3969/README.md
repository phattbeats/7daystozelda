# Workbench icon studio (#3969)

The Workbench rows show each kit as its own piece. These scripts make those icons
from the game itself, so they always match the models the pieces are built with.

1. Boot a local build with `tools/harness/m9/playd-gpu.py` and load a save past the
   intro (Link's house works: no cutscene, nothing in the way).
2. Run `capture.py` through `pc.sh` from this folder. For each subject it calls
   `sevendays_test_icon_studio(type, white, turn)`, which draws the piece through
   its own orthographic camera on a black, then a white, backdrop that hides the
   world, and saves `raw/<name>_b.png` and `raw/<name>_w.png`. Set `ONLY=[...]`
   first to redo some; `TURN` holds per-piece angles (the staircase shows its
   treads).
3. `python3 matte.py raw icons` keeps the difference as alpha, crops to the piece,
   and downscales to 32x32 RGBA.
4. `python3 ../../../art/icons/build_icon_pack.py --icons icons --o2r soh.o2r`
   packs them (see web/README.md).

The renders are of Nintendo models: keep `raw/` and `icons/` out of this repo.

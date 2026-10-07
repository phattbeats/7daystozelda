# Twinrova rig (PHA-4053)

Two local GPU Chromes and a stock Anchor relay, as in `tools/harness/pha4047`, with every path under
`/tmp/z4053` (tree `/tmp/wOOT-True-Co-op`). Setup from nothing took about an hour; the pieces:

- `fixpaths.py`, `sync.py`, `genlog.py`: relocate the prebuilt tree, copy the repo into it, rebuild `.ninja_log`
  (see agent memory zelda-buildtree-without-docker). `cmake .` once after new .cpp files.
- `vlibs.py <dir>`: fetches the Chrome runtime libs from Debian trixie (no apt in the container).
- Relay: `docker cp soh-anchor:/app/bin -` over `ssh raid`, byte-replace `:43383` with `:43453`.
- `serve/`: `docker cp soh-web:/app/public -`, then `install.sh` swaps in the build's soh.js/soh.wasm.
- ROM: `oot.o2r` from Nextcloud `PHATT-TECH/Projects/7daystozelda/rom/`. `boot.py` + `newfile.py` + `intro.py` + `save.py`
  make `file1.sav`/`global.sav` on one profile; `boot2.py` copies them into the other.

Driving a fight (`anchor_test_tw(cmd,arg)`, `anchor_test_tw_pos(which,x,y,z)`; see TwinrovaAdapter.cpp):

- `fresh.sh` rejoins both clients and walks them into room 3 of scene 0x17 (`warp:8D`, adult Link with the Mirror
  Shield via `TW(6)`, `anchor_test_load_room(3, ...)`); the intro starts when each Link is within 150 of the centre.
  The scene's actors only exist on the adult layer, in room 3.
- `hit1.py A|B`: Koume fires, the named client reflects with R held and faces Kotake; Kotake takes the hit.
- `merge1.py`: witches to 2 health each, the merge cutscene on both. `blast1.py B 1 2`: B's shield has 2 charge, absorbs a
  fire blast, releases it and stuns Twinrova. `dmg2.py N`: alternating sword swings on the stunned Twinrova.
- Logs: `playd-A.log` / `playd-B.log` (grep `TwinrovaSync`, `EVENT`). Canaries: `Fuzzy|No match|limb mismatch|split brain|parse error`.

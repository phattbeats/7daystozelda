# PHA-4071: placeables on the D-pad

Kits bind to D-pad directions. Pause menu, Workbench > Base: press C-Up/Down/Left/Right on a kit row to bind it to that D-pad direction (same direction again clears; the D-pad itself binds when it isn't moving the cursor). In play, a bound D-pad press starts placement; the same direction ends it, another bound direction swaps kit. The HUD D-pad shows the kit icon and pool count (also with "Equip Items on Dpad" off). Bindings are per-client cvars `gSevenDays.DpadKit.{Up,Down,Left,Right}`.

Verified in the wasm build (solo rig, scene 81): bind via test hook and via C keys, press starts placement (s2), swap, same-direction cancel, A places (placeables 112 -> 113, kit 6 -> 5, placement stays open), HUD shows icons and counts (s1), Base row tag `[Left]` (s5).

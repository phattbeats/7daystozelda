# #4006: 7DTZ balance: a ring of 12 torches stops raids from spawning, and the empty night still pays out

Found while writing the strategy guide (#3996). If 12 torches stand 750 from the workbench, one every 30°, every point of the 600–900 spawn band is within `TORCH_RADIUS` (300) of a torch. `SampleRing` keeps no points, `TrySpawnRaider` retries every 5 frames, the clock holds for 240 s, and at dawn the raid still counts as survived and pays out.

## Decision (Brandon, 2026-10-05)

Keep it as a secret, but give it mechanics: a ritual or cleansing happens; the players are told the raid is paused because they are warding it off; Navi talks about it afterwards, dumbfounded that someone figured it out; a niche NPC gives vague hints.

## What shipped

- **The ward** (`Raids.cpp`, `WardComplete`): a raid night (not Gohma's prologue night) is warded when every floor point of the spawn ring, checked on a grid (72 angles, every 50 units from 600 to 900), is within `TORCH_RADIUS` of a torch. The check runs at the wave's start and every 2 s while no raiders are alive, so lighting the last torch mid-night also closes it.
- **The ritual**: the ring's torches (600 − 300 to 900 + 300 from the workbench) turn blue one after another, sweeping round from Link's side in 4 s, each with an ignition sound (`TorchWardLit`, `Placeables.cpp`). Then the secret chime and the toast "Warded — The raid can't reach you while the ring burns." The toast comes back every 2 minutes.
- **The cleansing**: while warded, the red raid-night tint and the raid music go away (`RaidNightLook` in `Nights.cpp`), the clock is not held (new wave status `WAVE_WARDED`, sent to peers as `"warded"`), and the pause line reads "Tonight: warded".
- **Breaking it**: a torch broken or packed up opens the ring. "The ward is broken!", and the rest of the wave comes on with a fresh night hold.
- **Dawn**: a warded night still counts as survived and pays the same, as "The dead never came. Warded: +X Wood, +Y Bone." (`BaseState::nightWarded`, saved in `counters.night.warded`, set by `RAID_REPORT`'s `warded`).
- **Navi** (`RAIDLINE_WARD`, text 0x9713, once per save) at the first warded dawn: she can't believe someone figured out an old Sheikah warding.
- **The hint**: the man stuck on a Kakariko roof (texts 0x5050 by day, 0x5051 at night, `sWorldLines`). By day he asks you to come back when the stars are out; at night his grandpa's story: the old Sheikah lit a dozen fires in a ring, way out, with no dark between them.

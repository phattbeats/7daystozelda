# PHA-4063: base and remote-player draw distance

Brandon (2026-10-07): "increasing draw distance for player bases and players ... the pop in is jarring sometimes."

## What was cutting things off (inspected, not guessed)

| Layer | Finding |
| --- | --- |
| Draw cull | `SevenDays_Placeable` has `ACTOR_FLAG_UPDATE_CULLING_DISABLED` but not `DRAW_CULLING_DISABLED`, so `func_800315AC` culls its draw at `uncullZoneForward` 1000 + `uncullZoneScale` 350 along the view axis. Measured (probe, stock tier): farthest drawn piece 1349, nearest hidden 1356. A fort 600 across vanishes whole once you are ~2000 from its centre. |
| Remote players | `DummyPlayer` is an `ACTOR_PLAYER` spawn and keeps Player's `FLAGS` including `DRAW_CULLING_DISABLED`: puppets are never distance-culled (probe: drawn at 3095 units). What did cut off was the name tag: `nametag.cpp` stops at 663 units. |
| Actor spawning | Every piece of the scene's base is spawned on scene load (`actor->room = -1`, no distance gate). Collision actors are separate and unaffected. |
| Scene / room | Hyrule Field is one scene (81); castle drawbridge to field is the same scene. Lon Lon Ranch is a different scene, so pieces never show across that border (unchanged, known limit). |
| Network | No interest radius or pose expiry hides a puppet; `client.sceneNum` must match, and `online`/`isSaveLoaded` must be set. Unchanged. |
| Frustum / fog | Frustum margin stays 350 (uncullZoneScale). The camera far plane is `lightCtx.fogFar` (probe: 12800 at dusk/day, 8800 at night); nothing past it is ever visible, so the new range is capped by it. |
| Upstream setting | `gEnhancements.DisableDrawDistance` multiplies the cutoff for *every* actor in the world (and the field's Stalchildren, grass, etc. with it). It is not used: the new range is per category and leaves vanilla actors alone. A profile that has it >1 hides the stock behaviour, so test with it at 1. |

## What changed

* `gSevenDays.DrawRange` (0..3, default 2): forward cull distance of base pieces.
  0 = stock 1000, 1 = 2400, 2 = 4200, 3 = 7500, capped at the scene's far plane.
* Per category: a piece's range is the tier value times `clamp(size / 120, 0.45, 1)` (size = largest of 2*halfX, 2*halfZ, height): walls, floors, towers get the whole range, torches, chairs and signs 45-60%.
* Hysteresis: a piece that drew last frame keeps +15% so an edge piece cannot flicker.
* Far LOD: past 1350 units (the old cutoff) walls, floors, decks, steps and stairs draw as a tinted box of their footprint instead of the model. The full models only ever draw where they always did.
* Name tags of remote players follow the tier: 663 (tier 0), 1600, 2600, 4000 units; the text grows with distance (up to 3x) so it stays readable.
* Probe `sevendays_test_draw(cmd)` (`tier:N` sets the cvar): spawned / drawn / far-LOD counts, farthest drawn and nearest hidden view distance, per-puppet distance, drawn, tag.

Nothing about spawning, collision, authority or networking changed; this only moves the draw cutoff.

## Measured (4-client rig, Quadro K2200, headless Chrome, 960x540 observer)

Distances are from the fort centre along +x; the fort is 600 across. "drawn" is pieces drawn out of the scene's base.

94 pieces (the saved fort), stock vs default: see `docs/evidence/pha4063/sweep_a90.txt`. Tier 0 drew 66 of 94 at 900, 29 at 1500, 0 from 2200. Tier 2 drew 94 at 900/1500, 89 at 2200 (farthest drawn 3013), 77 at 3000 (farthest 3665), 63 at 4000.

256 pieces (maximum cap, `gSevenDays.BaseCap` 256, mixed kinds), observer fps, other clients shrunk to 320x180, 48 raiders live (rig fps is shared by four renderers and noisy, compare rows only):

| view distance | stock (tier 0) | tier 1 | tier 2 (default) | tier 3 |
| --- | --- | --- | --- | --- |
| 1000 | 5.0 fps, 171 drawn | | 4.5, 256 | 5.5, 256 |
| 1500 | 8.0, 84 drawn | 9.0, 226 | 4.0, 256 | 6.5, 256 |
| 2200 | 52, 0 | 40, 171 | 18, 241 | 8.0, 256 |
| 3000 | 60, 0 | 37.5, 88 | 24.5, 210 | 13.5, 256 |

Without the far LOD (first build, boxes only past 1900) tier 2 at 3000 ran 4.5 fps; with the LOD at 1350 it runs 24.5.

Combined large-base + raid gate (default tier 2, `DisableDrawDistance` 1, 4 clients in the fort, 48 raiders, pool scale 4):

* 94 pieces: 0 overflow frames, 0 guard trips, 0 piece/actor skips, POLY_OPA peak 60 KB of 391 KB, ~15-30 fps per client (`press94_t2_dd1.txt`).
* 256 pieces: 0 overflow, 0 skips, POLY_OPA peak 76-90 KB of 391 KB (minFree 301 KB), no crash; fps 4-16 per client with four full renderers on one GPU (`press256_t2_dd1.txt`), stock tier on the same state 6-18 (`press256_t0_dd1.txt`): standing in the fort the tier changes nothing, it is the fort itself that costs (see PHA-4062).
* Scene churn (Kakariko and back) x2 plus day / night / blood-moon cycling on all four clients with 256 pieces: all 256 actors back, no overflow (`churn256_t2.txt`).
* Save, reload, load file: 256 pieces back in Hyrule Field (`save_reload.txt`). Late arrival: a client rejoining mid-session gets 256 spawned actors and sees the other players tagged (`late_join.txt`).

## Tradeoffs and limits

* The draw range is not infinite: tier 3 stops at 7500 units or the scene far plane, small furniture at 45-60% of that. Pieces in another scene (Lon Lon Ranch from the field) are not drawn.
* Seeing a whole 256-piece fort from afar still costs frame time. At 1500 units the fort is mostly full-detail models and is as expensive as standing next to it; that is why a base of more than 160 pieces is an owner option (`BaseCap`). If that hurts on a weak device, `gSevenDays.DrawRange` 1 keeps stock-like cost out to 2200.
* Real phones and a low-end desktop were not available in the runner: only the K2200 desktop GPU rig was measured. Low settings: tier 0/1 and `GfxPoolScale` 1 are covered by the probes above (tier 0 = stock); no device-level check was done.
* Remote players never hid by distance (see above); the only change for them is the longer name-tag range. The probe shows them drawn at 3095 units; a visual capture of a distant puppet was not obtained (the camera does not track it at that size).
* Fog/daylight: at night the far plane is 8800, so tier 3's 7500 stays inside it; nothing drawn is wasted past it.

Rig scripts: `tools/harness/pha4063/` (`dd2.py` approach sweep with screenshots and probe, `dd3.py` tier loop, `playd-gpu.py` with the host-resolver port map). Evidence: `docs/evidence/pha4063/`.

## Deployed

Live `soh-web:pha4063` (`soh.js?v=cc7604cf`, soh.wasm md5 071e2445), built FROM `soh-web:pha4062`, game files only. Rollback: tag `pre-pha4063` = pha4062, old container kept stopped as `soh-web-pre-pha4063` (`docker rm soh-web && docker rename soh-web-pre-pha4063 soh-web && docker start soh-web && docker network connect phattvip soh-web`). The bundle was built from pha4062's source plus this change (commit f686193 is the same change rebased onto main with PHA-4071's D-pad commit, which the bundle does not contain; PHA-4071's deploy will pick this up when it builds from main). Public site serves the new bundle (checked through zelda.phatt.vip: page 200, soh.js and soh.wasm md5 match).

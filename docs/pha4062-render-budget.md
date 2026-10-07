# PHA-4062: display-list overflow, and what a 256-piece base costs

Status: shipped. Live as `soh-web:pha4062` (`soh.js?v=2e7d69b3`, code at 60537b8); rollback `soh-web:pre-pha4062` (= pha4046, `soh.js?v=4eba9b30`), old container kept stopped as `soh-web-pre-pha4062`. Everything below was measured on the PHA-4062 rig
(`tools/harness/pha4062/`): four headless Chrome clients with a local GPU, one relay, one box, so the
absolute frame rates are for that rig only. No real phone was available to the run; see "Not measured".

## The bug

A 94-piece base, a raid, and a normal player setting (`gEnhancements.DisableDrawDistance` = 4) crashed
every client of the room within a second:

    interpreter.cpp:4154 [critical] Unhandled OP code: 0x66, for loaded ucode: 4
    Received signal 11 SEGV_ACCERR            (Playwright: "Target crashed")

Reproduced on stock main (42e8dc0, the live `soh-web:pha4046` code) with `tools/harness/pha4062/repro.sh`:
94 pieces (fort2), 4 clients, about 50 raiders (`anchor_test_mini spawn`), draw distance 4. Evidence:
`docs/evidence/pha4062/baseline-main-42e8dc0-crash.txt`.

### Root cause

- The game draws into four fixed lists per frame: POLY_OPA (0x2FC0 commands), POLY_XLU (0x1000), overlay
  (0x800) and work (0x100). The `POLY_*_DISP++` macros write with no bounds check.
- POLY_OPA also holds everything `Graph_Alloc` hands out from the far end of the same buffer: every
  `MATRIX_NEWMTX`, every `Lights` block. A list that grows up meets the allocations that grow down and
  overwrites them, or the other way round.
- `Graph_Update` noticed (`THGA_IsCrash`) and skipped `Graph_TaskSet00`, but the browser build's
  `RunFrame` then calls `Graph_ProcessGfxCommands(workBuffer)` regardless, and `RunFrameWeb` keeps
  re-rendering that list on every animation frame. The renderer interpreted clobbered commands:
  "Unhandled OP code", then a wild read.
- The stock buffers are the N64's (98 KB for POLY_OPA). Upstream Ship of Harkinian already replaced them
  with 1M-command pools (`z64.h`); this fork was still at the original sizes.
- Most of POLY_OPA is matrices, not commands: with only the 94-piece fort in view, 34 KB of the 47 KB used
  was the tail (about 540 matrices). A piece costs about 265 bytes of pool (one matrix, a few commands), more
  for pieces with several parts (scarecrow about 2 KB, palisade 590 B).

Measured demand on the heaviest client of that scenario (the one standing among the raiders): POLY_OPA
123 KB, 126% of the stock 98 KB. With only the base and a few players it is 31-60 KB.

## What changed

`soh/src/code/graph.c`
- The four pools are static arrays at up to 8x stock, fenced by guard words, and `Graph_InitTHGA` sets up
  `GFX_POOL_SCALE_DEFAULT` = 4x (POLY_OPA 391 KB, POLY_XLU 131 KB, overlay 65 KB, work 8 KB, two of each,
  2.4 MB of static data at the 8x ceiling). `gSevenDays.GfxPoolScale` (1-8) picks the size in use per frame,
  so a build can be run at stock size to exercise the failsafe.
- `Graph_GfxCheckFrame` runs on every frame: it records head/tail use and minimum free bytes per pool and
  checks the guard words. A frame whose list ran into its own allocations (or past a pool, or trampled a
  guard) is replaced by an empty root list (`gSPEndDisplayList` at the start of the work buffer) and counted
  (`overflowFrames`). It never reaches the renderer.
- `Graph_GfxRoomLow` / `Graph_GfxRoomBelowPercent`: the room left in POLY_OPA/POLY_XLU, used below.

`soh/src/code/z_actor.c`
- `Actor_Draw` returns without drawing when fewer than `ACTOR_DRAW_RESERVE_CMDS` (2048 commands, 16 KB)
  are left in POLY_OPA or POLY_XLU. The reserve covers one worst-case actor draw plus the effects, HUD and
  sync commands that follow the actor loop, so an over-budget frame sheds actors instead of overrunning.
  Skips are counted (`actorSkips`).

`soh/soh/SevenDays/Placeables.cpp`
- A piece skips its own draw below 35% free in either pool (`kPieceDrawFloorPercent`), earlier than the
  engine reserve. Pieces sit early in the actor list (ACTORCAT_BG), so without this a big base would use up
  the room that enemies, Link's gear and effects need, and the raid would vanish before the walls did.
- `sevendays_test_gfx(cmd)`: `""` reads, `"reset"` clears the peaks, `"force:N"` makes the next N frames count
  as overruns (the backstop path). Output is JSON: sizes, peak head/tail/used, minimum free per pool,
  `overflowFrames`, `guardTrips`, `actorSkips`, `placeableSkips`.

Collision and capacity (`SevenDays.h`, `Base.cpp`, `Placeables.cpp`)
- `DYNA_BUDGET` 4096 -> 8192 polygons, vertices and poly nodes for outdoor scenes (about 210 KB of the play
  arena; 2.0 MB of it was free in every run). At 256 mixed pieces the lists hold 4896 polygons, 3264
  vertices and 4961 nodes, which the old 4096 could not (the node list returns `SS_NULL` when full, so
  collision silently disappears).
- `PieceCollisionCost` and `CollisionFits`: the owner refuses a placement when the scene's pieces would
  use more than 90% of the lists (polygons, vertices, and nodes estimated at 1.25 per polygon; measured 1.01-1.03).
- `BASE_CAP` is now `BaseCap()`: `BASE_CAP_DEFAULT` = 160 unless the owner's `gSevenDays.BaseCap` says up to
  `BASE_CAP_MAX` = 256. The counting rule is unchanged (per scene and era).
- The base probe (`sevendays_test_base`) also reports `refusals` (why the owner refused placements) and
  `dyna.polys / verts / chunks / vtxMax`.

## Is 256 pieces too much?

The engine side holds it. Frame time is what does not.

| Subsystem | At 256 mixed pieces | Verdict |
| --- | --- | --- |
| Piece ids / counts | `uint16_t` ids wrapping at 0x7FFF, seeded decor from 0xF000; no `u8` count or index anywhere | fine; 255/256 is not a boundary |
| Save | one JSON blob (`sevenDays` v2 `base`), no fixed array | fine; no version bump, old saves (at most 100 per scene) load unchanged |
| Network | full state is one JSON array (~100 B/piece, ~26 KB); deltas are per piece with a rev check; relay packet cap is 4 MB | fine; late join tested |
| Actors | 256 piece actors + 10-24 collision chunk actors vs `ACTOR_NUMBER_MAX` 2000 | fine |
| Collision | 10 chunks of 14 BG slots (of 50); 4896 polys, 3264 verts, 4961 nodes of 8192 | fine at 8192 (4096 overflowed) |
| Display-list pools | worst client 144-181 KB POLY_OPA of 391 KB (37-46%), POLY_XLU 16-18 KB of 131 KB | fine at 4x; over stock size at 94 pieces already |
| Wasm heap | 368.8 MB before and after, JS heap 87-133 MB | no change |
| Frame time | see below | the limit |

Frame rate on the rig (client B standing at the gate, 24 raiders, default draw distance, the other three
clients rendered at 320x180 so they do not share B's GPU; fresh rig for each row, 30 s mean):

| Pieces (mixed) | fps | POLY_OPA peak |
| --- | --- | --- |
| 0 | 54 | 31 KB |
| 94 | 23 | 87 KB |
| 128 | 16 | |
| 160 | 19 | 107 KB |
| 192 | 6 | |
| 224 | 4 | |
| 256 | 7 | 144 KB |
| 160, all palisades/Baba/scarecrow/torch | 35 | 60 KB |

The mixed base falls off between 160 and 192 pieces; a base of cheap pieces does not. Per piece cost
differs by model, not by pool bytes (`typecost.out`): ranch floor, inn bed, stairs, mayor's bed and the
scarecrow are 3-10x the frame time of a palisade, deck or torch. The pool bytes per piece are nearly
constant (265 B), so the pools cannot tell the expensive pieces apart.

**Verdict.** 256 is a valid target for every subsystem except frame time on this rig. The default is 160;
the owner can raise it to 256 (`gSevenDays.BaseCap`) once real-phone numbers exist. A cost-weighted cap
(counting a ranch floor as several palisades) would let cheap bases go to 256 while capping costly ones; the
per-type table in `typecost.out` is the starting point.

## Not measured

- A real phone. This runner has none; the frame rates above are a desktop headless Chrome with a local GPU
  and four clients on one box, so treat them as relative.
- The increased base/player draw distance (PHA-4063) had not landed. `DisableDrawDistance` = 4 stands in for
  it: it draws every actor in the room, which is the pressure that change adds. Re-run
  `tools/harness/pha4062/pressure.sh` on the combined build.

## Tests (build 2e7d69b3, `soh-web` candidate; logs in `docs/evidence/pha4062/`)

Method: `final.sh`, `compat.sh`, `repro.sh`, `perf.sh`, `typecost.py` in `tools/harness/pha4062/`. A, B, C, D are four
connected Links in one room, A is the owner and raid director.

| Check | Result |
| --- | --- |
| Stock main (42e8dc0), 94 pieces, ~50 raiders, draw distance 4 | A, B and C crash within 1 s: `Unhandled OP code: 0x66`, `SEGV_ACCERR` (`baseline-main-42e8dc0-crash.txt`) |
| Same scenario, fix build, pools at 4x | no crash; worst client POLY_OPA 123 KB (126% of stock), 0 overflow frames |
| Same scenario, fix build forced back to stock-size pools (`GfxPoolScale` 1) | no crash; the heaviest client skipped 23,125 actor draws (min free 14 KB), 0 overflow frames |
| 94 pieces (final build), 48 raiders, draw distance 4 | no crash, 0 overflow, 0 skips; 5-47 fps by client (B at the gate, full size, 4 clients on one box) |
| Default cap | the owner refused 351 placements as "The base is full (160 pieces)" with no `BaseCap` cvar |
| `gSevenDays.BaseCap` 256, 256 mixed pieces, 48 raiders, draw distance 4 | placed 256/256, 256 actors, 9-10 chunks, 5568 polys / 3712 verts / 5633 nodes of 8192; no crash; worst client POLY_OPA 183 KB of 391 KB, 4 fps |
| Failsafe, 60 forced overrun frames on every client | all four counted 60 `overflowFrames`; the tabs stayed up and the game kept running (frames kept advancing, fps recovered) |
| 3 rounds of Kakariko and back to Hyrule Field, with day / night / blood moon (raid forced, moon red 255) on every client | 256 actors and identical collision lists after every return; no crash |
| Late join: a fifth client (adult) joins the 256-piece room | sees 256 pieces; in Hyrule Field it spawns 256 actors with the same dyna lists |
| Save / reload: owner saves 256 pieces, reloads the page, loads the file | 256 pieces, 256 actors, same collision |
| Stock-build save (94 pieces, written by main 42e8dc0) loaded on the fix build | 94 pieces, 94 actors, same collision lists |
| Vanilla pass (7 Days off) on the same four-client room | 59-60 fps on every client, 0 overflow, 0 skips, peak POLY_OPA 16 KB |
| Memory | wasm heap 368.8 MB on stock and fix builds; JS heap 87-133 MB; play arena 2.0-2.2 MB free with the larger collision lists |

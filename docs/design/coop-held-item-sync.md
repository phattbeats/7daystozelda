# Co-op held and thrown item sync (#4180)

Brandon, 10/11: a partner's bomb was neither synced nor visible while carried. Link's carry
pose always synced (`stateFlags1`, joint table), but the thing in the hands is its own actor
on each client, and those were never sent.

## Audit

"Before" is main at e698d06. "Now" is this issue.

| Item | Before | Now |
|---|---|---|
| Carry pose (arms up, throw wind-up) for any lifted actor | Synced (PLAYER_UPDATE) | Unchanged |
| Bomb in hand | Partner saw the pose with empty hands | **Fixed**: ghost bomb at the puppet's hands, own fuse, flash |
| Bomb placed or thrown, in flight | Invisible to the partner | **Fixed**: ghost follows the owner's bomb |
| Bomb explosion | Only the owner saw the blast, flash and shake | **Fixed**: the ghost blows up with the same fuse (screen flash, sound, rumble); it can't damage or break anything |
| What a bomb destroys | Walls and rocks already sync through their flags; damage is the owner's | Unchanged |
| Bombchu running, and its blast | Invisible | **Fixed**: ghost chu follows the stream; the blast is the owner's EN_BOM in the stream |
| Pot, crate, rock, bush carried | Pose only, with the original still on the ground | **Fixed**: ghost in the hands; the partner's original is hidden and frozen while it is out |
| Pot, crate, rock, bush thrown | Invisible; the partner's original broke when the thrower's did | **Fixed**: ghost flies; on the break the ghost vanishes with the break sound and the hidden original is removed without a drop |
| Deku stick equipped | Model synced (modelGroup) | Unchanged |
| Deku stick length, lit flame, burn-down | Not sent: the puppet showed a full unlit stick | **Fixed**: `stickLength`, `stickFlame` in PLAYER_UPDATE |
| Bombflower carried | Not synced | **Open**: a plant is a world object that exists on every client; carrying it needs its own claim rules |
| Deku nuts, slingshot seeds, arrows, boomerang, hookshot, magic | No stream. Hits and damage already sync through the enemy hit path | **Open**: cosmetic only. The same ghost mechanism covers them, with the hookshot chain and boomerang return drawing against the local Link, so each needs its own pass |
| Held pot or rock fragments on break | n/a | **Open**: the ghost vanishes with the sound, but the fragments are not spawned (the spawn is static in each actor) |

## How it works

`Packets/HeldItemSync.cpp`, packet `HELD_ITEMS`, sent every frame to partners in the same
scene while something is out.

- **Owner**: streams bombs and chus the local player took out (a bomb first seen within 110
  units of Link, or where one of the player's chus just blew up), and any pot, crate, rock
  or bush Link carries or has thrown (up to 160 frames after release). Bombs raids, bosses and
  bombflowers make are on every client already and are not streamed.
- **Partner**: spawns a ghost actor per item (`Actor_Spawn`, room -1).
  - Bomb ghosts run their own update, so the fuse, flash, sound and blast come for free, and
    their position follows the owner's bomb until it explodes. After every update their AT/AC/OC
    colliders are switched off and the local player's `interactRangeActor` is cleared, so the
    ghost hurts nothing, is hit by nothing and can't be lifted.
  - Chu and prop ghosts skip their update (`ShouldActorUpdate`) and take the streamed pose.
  - A stale stream (6 frames) ends the ghost. A bomb in its fuse finishes as a harmless
    blast; a chu is removed; a prop gets a break sound, and its hidden original is killed (or
    shown again if the partner left the scene).
- **Sticks**: PLAYER_UPDATE carries `unk_85C` and `unk_860`; the puppet takes them in
  `DummyPlayer_Update`.

CVar `gRemote.Anchor.HeldItemSync` (default on) disables it.

## Test hook

`anchor_test_held(cmd, a, b, c)` (wasm export): 0 dump (owned, ghosts, hidden originals),
1 spawn a bomb at Link (a = fuse), 2 spawn a bombchu, 3 spawn a prop and treat it as carried
(a = 0 pot, 1 crate, 2 rock, 3 bush), 4 move it by (a, b, c), 5 kill it.

Status: compiled (`-fsyntax-only` against the PHA-4167 tree); two-client rig test pending in
the batched build.

## Princess Ruto (PHA-4195)

Ruto (`EnRu1`) is one persistent NPC per client (`room = -1`), so she was never mirrored: the carrier's Ruto rode on their Link while the partner's copy stayed where she sat, or was already gone (her action 27 kills her once her room is left). She now rides the same `HELD_ITEMS` stream with her action and room numbers (`ra`, `rm`):

- Owner: streams her while she is lifted (31/32/34) and while she flies or sinks after a throw (28-30); the stream stops when she comes to rest.
- Partner: takes over their own Ruto (update frozen, pose from the stream, sitting loop animated, drawn opaque). If they have none, a type-10 stand-in is spawned (no flag or duplicate checks); it is swapped for the partner's own Ruto if hers loads.
- When the stream stops she is dropped at the owner's last position in state 28 with the owner's room numbers. She lands and offers herself to be carried, or lands on the Sapphire platform and starts that cutscene on the partner's screen too. A Ruto that was in a talk/cutscene state, or an owner who left, puts her back as she was.
- Both players lifting their own Ruto: each keeps theirs (not mirrored).
- Test hook: `anchor_test_held(8, dist)` puts a lift-ready Ruto `dist` ahead of Link; the dump lists `rutos` with action/draw/ghost/adopted/rooms.

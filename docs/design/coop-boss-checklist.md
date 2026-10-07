# Co-op boss checklist (PHA-4043)

Every boss gets "the Gohma treatment": an `ActorSyncAdapter` in
`soh/soh/Network/Anchor/BossAdapters/` (template: `GohmaAdapter.cpp`, fullest
example: `KingDodongoAdapter.cpp`). A boss is done when every row below holds
in a two-client live test, with screenshots and logs. The rig is in
`tools/harness/pha4047/`.

## The checklist

1. **Phases.** `GetPhase` reads fields, never `actionFunc`. The intro runs
   locally on each client; `ShouldMirror` is false until the local intro is
   over and the streamed phase is FIGHT. A client that arrives late plays its
   own intro, then mirrors.
2. **Extras.** Stream what Draw reads but only Update computes (colours,
   scales, spins, offsets, sub-part state, counters other code reads). Never
   stream anything that spawns pieces from Draw or PostLimbDraw. If the room
   itself animates from the boss's Update (lava, light), run that part on the
   mirror too.
3. **Health.** If the boss keeps its own health field (not
   `colChkInfo.health`), stream it in extras. Check that hits from the mirror
   player reach the host (`HITREQ`) and that the boss's own damage code
   accepts them.
4. **Defeat.** FIGHT -> DEFEATED calls the boss's own death setup locally and
   returns true. `OnRemoteDefeat` covers a missed edge. Both clients get the
   death cutscene, heart container, blue warp and clear flag. If the death
   arrives while a client's own intro still owns the camera, wait for the
   intro to end.
5. **Children.** Each projectile, minion and sub-part is either a tracked
   dynamic spawn or excluded as a local effect (`IsTrackingExcluded`), and the
   choice is written down. Anything the mirror player does to the boss that no
   collider carries goes to the host as an adapter event
   (`EnemySync::SendAdapterEvent` -> `OnRemoteEvent`).
6. **Aggro.** Bosses stay out of the EnemyTargeting puppet swap. Decide per
   boss whether its attack choices can include the other players (as King
   Dodongo's do through `Anchor_BossAimTargets`) or stay host-only, and give
   the reason.
7. **Resume.** `OnLocalResume` puts the boss back into a state the local AI
   can continue from, for when the stream goes stale or this client becomes
   the host.
8. **Live test.** Intro, fight and defeat on two clients: no desync canary in
   either log, no crash, both players land damage and both take hits.

## Status

| Boss | Issue | Status |
|---|---|---|
| Gohma | M3, PHA-4023 | Done (warp spot and crash handoff: PHA-4046) |
| King Dodongo | PHA-4047 | **Done**, see below |
| Barinade | PHA-4048 | Not started |
| Phantom Ganon | PHA-4049 | Not started |
| Volvagia | PHA-4050 | Not started |
| Morpha | PHA-4051 | Not started |
| Bongo Bongo | PHA-4052 | Not started |
| Twinrova | PHA-4053 | Not started |
| Ganondorf and Ganon | PHA-4054 | Not started |

## King Dodongo (PHA-4047)

| Row | How | Live test (2026-10-06, two local clients) |
|---|---|---|
| Phases | Health <= 0 is DEFEATED; otherwise `unk_1BC` (non-zero while a cutscene owns him) splits PREFIGHT from FIGHT. | Both intros ran locally. B voided out, came back and replayed its own (shorter) intro, then mirrored. |
| Extras | Health, roll spin and tilt, belly and squash scale, shape offset, hit flash, fog colour, fire glow and screen tint, fire light, shrunk spheres, inhale and flame counters, corner and direction. The mirror runs `BossDodongo_UpdateAmbience` (lava waves and bubbles, fire light, wobble) and skips the magma, which EnemyFxSync replays. | Same pose and red glow on both screens. |
| Health | His `health` is an s16 of his own; streamed as `hp`. | A's swings and B's forwarded swings each took 1; bombs took 2. Both clients showed the same health at every step (12 -> 0). |
| Defeat | `SetupDeathCutscene` + `Enemy_StartFinishingBlow` on the edge. A client still in its intro defers it (`Anchor_KingDodongoDefeatPending` at the end of the intro). | Killing blow from the mirror: both death cutscenes ran in lockstep and ended with the blue warp and the clear flag on each. Deferred case: B watched its own intro, then the death, and got its camera back; each client then had exactly one blue warp and one heart container. |
| Children | EN_BDFIRE (fire breath) is excluded; each machine spawns its own flames from the streamed count, and each flame burns only its own Link. Bombs aren't synced, so the mirror reports a swallowed bomb (`KD_EVENT_SWALLOW`) and the host explodes him if he is still inhaling. BG_BREAKWALL, the heart container and the warp come from the local death. | B's bomb was swallowed on B's screen at inhale 31 and taken by the host at 33. |
| Aggro | No puppet swap: the intro, the death and the lava move or read GET_PLAYER. His attacks don't aim at a position, so `BossDodongo_UpdateAim` feeds every living player into his choices (breathe fire at whoever is in his lane, turn back for whoever is behind, keep walking while anyone is ahead). Alone, the checks are vanilla. | With only B in his lane, the host's copy breathed fire at B; B burned and A didn't. |
| Resume | `SetupWalk` from the streamed pose. | Not hit in the test (no stale stream). |
| Live test | | No desync canary in either log, no crash. Both players were hit by the roll on their own screens. |

Known limits:
- If the mirror's player fights a local King Dodongo while the host hasn't
  dropped in yet, that damage is lost when the host starts streaming.
- A test warp into the boss room doesn't pull the partner in; boss co-entry
  needs the real door.
- Camera shake from his roll and wall hits plays only on the host.

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
| Barinade | PHA-4048 | **Done**, see below |
| Phantom Ganon | PHA-4049 | Not started |
| Volvagia | PHA-4050 | **Done**, see below |
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

## Barinade (PHA-4048)

One actor id, 19 parts (body -1, supports 0-2, zappers 3-5, Baris 6-15, stumps 16-18, door 19) sharing file-static fight state.

| Row | How | Live test (2026-10-06, two local clients) |
|---|---|---|
| Phases | Read from the shared statics: PREFIGHT until `sCsState` reaches BATTLE, FIGHT until the death cutscene, DEFEATED from `DEATH_START` / `PHASE_DEATH`. Every part mirrors only after its local intro and while the host is fighting. | Both intros ran locally; the mirror took over at cs 13 on both runs. |
| Extras | Per part: shape offset, aim rotation, colour filter, pulse scales, glow, head, neck and arm vectors, dead and burst flags. Body only: the shared statics (`sCsState`, `sFightPhase`, `sBodyState`, `sPhase4HP`, `sPhase2Timer`, door, zapper rotation, Bari slots), the live-Bari and stump masks and the body collider's damage mask. The mirror ticks the shared effect array (`BossVa_UpdateEffects`). | Same pose, sparks and phase on both screens at every step. |
| Health | There is no health field. The body's phases are driven by hits (supports cut, `sKillBari`, `sPhase4HP`), so mirror hits go to the host. The damage table's effect for the replayed flags is applied (the boomerang stun is damage effect 1), and the boomerang is replayed with a stand-in EnBoom (`RemoteHitAttacker`). | Supports cut by A and by B; Baris killed by both; B's boomerang stun dropped the body on both screens; phase 4 hits from both took `sPhase4HP` 4 -> 0. |
| Defeat | `OnPhaseChange` FIGHT -> DEFEATED calls `BossVa_SyncStartDeath` (the body's `SetupBodyDeath` without the finishing-blow hooks the host already fired) and returns true, so every part runs its own death code. `HandlesDefeat` keeps the generic one-actor defeat handoff off the other parts. `OnRemoteDefeat` covers a missed edge. | Both clients ran the death cutscene (cs 14 -> 24), spawned one heart container and one blue warp each, and set the clear flag. |
| Children | Baris are tracked under a key derived from their params (`BarinadeKey`): each client spawns its own, the host from the AI and the mirror from the Bari mask, so nothing is broadcast and nothing double-spawns. Supports are cut on the mirror by `BossVa_SyncCutSupport` (cut skeleton, cut action, stump), which the death cutscene's burst steps run inside. Stumps and the door are excluded (`IsTrackingExcluded`); sparks, tumours and lightning are local effects. | Bari and stump sets matched on both clients at every step. |
| Aggro | Host-only. His AI reads the host's Link where an actor swap can't reach (`stateFlags1` DAMAGED and `invincibilityTimer` gate the zapper charge and the chase, the body turns on `yawTowardsPlayer`), the Baris orbit the body, and every damaging collider is mirrored, so each player is hurt by what they actually touch. Retargeting would make the zappers' charge and cooldown depend on whichever player is nearest from frame to frame, which the shared statics cannot hold. | Body contact hurt both players on their own screens. |
| Resume | Parts the stream stops covering resume their own AI; a Bari the host removed quietly is killed after a grace period (`ABSENT_BARI_GRACE`). | Not hit in the test (no stale stream). |
| Live test | | No desync canary in either log, no crash, full run intro -> fight -> defeat on both clients. |

Known limits:
- The mirror doesn't receive the host's spark, tumour and lightning-charge spawns (it ticks the shared array only).
- A test warp into the boss room doesn't pull the partner in; boss co-entry needs the real door.

## Volvagia (PHA-4050)

Boss_Fd (flying) and Boss_Fd2 (hole form) are both registered. They share one health pool (Fd's `colChkInfo.health`, spent by Fd2's collision check).

| Row | How | Live test (2026-10-06, two local clients) |
|---|---|---|
| Phases | Fd2 and Fd: health <= 0 is DEFEATED; a running intro (`introState != BFD_CS_NONE`) is PREFIGHT; otherwise FIGHT. `ShouldMirror` is false until the local intro is over and the stream says FIGHT (`Anchor_VolvagiaIntroOver` marks the end). | Both clients ran their own intro, then mirrored. |
| Extras | Update-computed draw fields: joint and segment tables, burrow and breath state, face exposure, hit flash, scales. The mirror runs `BossFd_MirrorUpdate` / `BossFd2_MirrorUpdate` for the visual-only parts. Nothing that spawns pieces from Draw is streamed. | Fd2 pose, action and fire matched on both screens. Fd flight looked identical; sampled position error (median ~73 units) is sampling skew from the 0.45 s probe, not a visible desync. |
| Hole | Fd2 picks its hole from the streamed state, so every client uses the authority's choice. | Same hole on both clients. |
| Hammer stun | A mirror's hammer hit becomes a HITREQ and the host applies it through its own collider code (health -2, face exposed, stun), then streams the result. | A hammer hit from the mirror stunned and exposed the face on both. |
| Defeat | On FIGHT -> DEFEATED the mirror runs `BossFd2_StartDeathHandoff` (the boss's own death setup) and returns true. `OnRemoteDefeat` covers a missed edge. A client still in its intro defers the defeat until the intro ends. | Killing blow from the mirror: both clients ran the death, got the blue warp and the clear flag. Late-intro deferral passed after fixing the health check. |
| Children | Falling rocks (EN_BDFIRE-style debris) and bones are each client's own, spawned from the streamed rock timer; fire breath is respawned locally from the streamed breath state and burns only the local Link. EN_VB_BALL is excluded from tracking. | About 12 rocks on A and 11 on B in the same window. |
| Aggro | No puppet swap. `BossFd_AimTarget` and `BossFd2_AimTarget` (via `Anchor_BossNearestTarget`) aim attacks at the nearest living player on the authority. The mirror never aims; it follows the stream. | Attacks followed the nearer player. |
| Live test | | Intro, fight, defeat from either side: no desync, no crash, both players damage it and both are hit. Rig: `tools/harness/pha4050/`. |

Known limits:
- Fd2's emerge knockback still pushes only the authority's Link.
- The 7DtZ mod swaps the heart container for a blueprint, so the "A blueprint!" message shows instead of a heart.

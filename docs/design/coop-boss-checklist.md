# Co-op boss checklist (#4043)

Every boss gets "the Gohma treatment": an `ActorSyncAdapter` in
`soh/soh/Network/Anchor/BossAdapters/` (template: `GohmaAdapter.cpp`, fullest
example: `KingDodongoAdapter.cpp`). A boss is done when every row below holds
in a two-client live test, with screenshots and logs. The rig is in
`tools/harness/pha4047/`.

## The checklist

1. **Phases.** `GetPhase` reads fields, never `actionFunc` (an `OnRemoteEvent`
   verdict check may read it for a one-shot timing window, as King Dodongo's
   swallow does, but it must never decide what is streamed). The intro runs
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
| Gohma | M3, #4023 | Done (warp spot and crash handoff: #4046) |
| King Dodongo | #4047 | **Done**, see below |
| Barinade | #4048 | **Done**, see below |
| Phantom Ganon | #4049 | **Done**, see below |
| Volvagia | #4050 | **Done**, see below |
| Morpha | #4051 | **Done**, see below |
| Bongo Bongo | #4052 | Not started |
| Twinrova | #4053 | **Done**, see below |
| Ganondorf and Ganon | #4054 | **Done**, see below |
| Minibosses (Dark Link, Iron Knuckle, Dead Hand, Big Octo, Flare Dancer, Stalfos, Lizalfos) | #4055 | See the miniboss section below |

## King Dodongo (#4047)

| Row | How | Live test (2026-10-06, two local clients) |
|---|---|---|
| Phases | Health <= 0 is DEFEATED; otherwise `unk_1BC` (non-zero while a cutscene owns him) splits PREFIGHT from FIGHT. | Both intros ran locally. B voided out, came back and replayed its own (shorter) intro, then mirrored. |
| Extras | Health, roll spin and tilt, belly and squash scale, shape offset, hit flash, fog colour, fire glow and screen tint, fire light, shrunk spheres, inhale and flame counters, corner and direction. The mirror runs `BossDodongo_UpdateAmbience` (lava waves and bubbles, fire light, wobble) and skips the magma, which EnemyFxSync replays. | Same pose and red glow on both screens. |
| Health | His `health` is an s16 of his own; streamed as `hp`. | A's swings and B's forwarded swings each took 1; bombs took 2. Both clients showed the same health at every step (12 -> 0). |
| Defeat | `SetupDeathCutscene` + `Enemy_StartFinishingBlow` on the edge. A client still in its intro defers it (`Anchor_KingDodongoDefeatPending` at the end of the intro). | Killing blow from the mirror: both death cutscenes ran in lockstep and ended with the blue warp and the clear flag on each. Deferred case: B watched its own intro, then the death, and got its camera back; each client then had exactly one blue warp and one heart container. |
| Children | EN_BDFIRE (fire breath) is excluded; each machine spawns its own flames from the streamed count, and each flame burns only its own Link. Bombs aren't synced, so the mirror reports a swallowed bomb (`KD_EVENT_SWALLOW`) and the host explodes him if he is still inhaling. BG_BREAKWALL, the heart container and the warp come from the local death. | B's bomb was swallowed on B's screen at inhale 31 and taken by the host at 33. |
| Aggro | No puppet swap: the intro, the death and the lava move or read GET_PLAYER. His attacks don't aim at a position, so `BossDodongo_UpdateAim` feeds every living player into his choices (breathe fire at whoever is in his lane, turn back for whoever is behind, keep walking while anyone is ahead). Alone, the checks are vanilla. | With only B in his lane, the host's copy breathed fire at B; B burned and A didn't. |
| Resume | `SetupWalk` from the streamed pose. | Not tested live: authority loss, stale streams and host handoff mid-fight were never exercised (PHA-4047 review). Per-fight flags (`pendingDefeat`) are now reset in Init/Destroy via `Anchor_KingDodongoReset`. |
| Live test | | No desync canary in either log, no crash. Both players were hit by the roll on their own screens. |

Known limits:
- If the mirror's player fights a local King Dodongo while the host hasn't
  dropped in yet, that damage is lost when the host starts streaming.
- A test warp into the boss room doesn't pull the partner in; boss co-entry
  needs the real door.
- Camera shake from his roll and wall hits plays only on the host.

## Barinade (#4048)

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

## Volvagia (#4050)

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
## Morpha (#4051)

| Row | How | Live test (2026-10-07, two local clients, fresh room so the intro runs) |
|---|---|---|
| Phases | Core `csState` and hp: death cutscene or hp <= 0 is DEFEATED, an intro `csState` is PREFIGHT, battle is FIGHT. `ShouldMirror` is false until the local core is in `MO_BATTLE` and the streamed phase is FIGHT. | Both intros ran locally (~42 s) and both reached FIGHT with the mirror on. |
| Extras | Core: hp, hit count, water level, flash, scale. Tentacles: action state, victim flags, shape and joint table (`jt`), colour and draw state. The core and tentacles are all ACTOR_BOSS_MO, so the first tentacle keeps its room-occurrence key (`OnEnemyActorSpawn` skips it: each client's core Init spawns it) and only tent2 (hit count >= 3, spawned from Update) is a dynamic spawn. Deserializers type-check every field; a wrong JSON type throws a C++ exception that kills the page (`da` is a u8, read it as a number). | Tent1 has the same static key on both clients and mirrors in lockstep; water level -63 on both. |
| Health | Streamed as `hp`/`hc`. Mirror hits go out as ENEMY_HIT_REQUEST and the host applies them. | B's hit took 20 -> 19 on both clients; A's own hit 19 -> 17. |
| Defeat | `BossMo_StartDeath` on the DEFEATED edge; `OnRemoteDefeat` covers a missed edge; deferred while the local intro runs. | Killing blow from B: cs 105, heart container, blue warp and clear flag on both clients. |
| Children | Tentacle 1 is static, tentacle 2 is tracked as a dynamic spawn. No other child actors. | Tent1 duplicate (a SPAWN on top of the local one) found and fixed during the test. |
| Aggro | Attacks aim at the nearest living player (not host-only), because the grab is a local effect on Link. The host cannot drive a remote player's Link, so the grabbed player's own client runs the hold (`MO_VictimDriver`: lift, shake, health drain) and the host waits for `MO_EVENT_ESCAPE` or its own timer. The intro stays local. | B was held, lifted and drained (48 -> 8), mashed free (ESCAPE sent, host released the tentacle to RETREAT); an un-mashed grab was released by the host timer. A saw B's held pose. |
| Hookshot | A hookshot hit on the core while it is in ATTACK cuts tent1 and stuns it, via the normal forwarded hit. | B's hookshot-flag hit: core ATTACK -> tent CUT (100) -> core STUNNED (5) on both. Caveat: the real hookshot's pull of the core is cosmetic on a mirror, because the stream overwrites the core position. |
| Live test | | No desync canary and no crash in either log after the fixes. |

Review fixes (PHA-4043 findings, 2026-10-10): `ci`/`mi`/`sp` from the network are range-checked (an out-of-range packet leaves the tentacle's cut index untouched and the page alive); a grab the victim's machine cannot take sends the tentacle back to ready instead of shaking an empty hold; only the held player's escape event is accepted; the pending-defeat flag and victim table reset in core Init/Destroy. Live: two-client intro -> fight -> grab -> defeat, one heart container, one blue warp and the clear flag on each client, no canaries (`docs/evidence/pha4051-fix/`).

Open: defeat while a victim is held, and the bandwidth of the `jt` arrays, were not measured.

## Phantom Ganon (#4049)

Boss_Ganondrof plus its horse (En_fHG, the painting-ride phase) and the energy ball (En_Fhg_Fire, params 50).

| Row | How | Live test (2026-10-07, two local clients) |
|---|---|---|
| Phases | INTRO / FIGHT / DEFEATED from `deathState` and a new `introOver` field (set when the local intro and the horse's cutscene finish). `ShouldMirror` is false until the local intro is over and the stream says FIGHT. `Anchor_GanondrofIntroOver` marks the end. | Both clients ran the full intro in lockstep, then B streamed and A mirrored. |
| Extras | `flyMode`, action code, leg and arm angles, eye brightness and alpha, invincibility and shock timers, flags, the horse pose (`hz`) and a ring of recent spawn events (`ev`) that the mirror replays (lightning, spear light). `GND_PositionCollider` re-offsets the body cylinder on the mirror, as `BossGanondrof_Update` does by hand. Nothing that spawns from Draw is streamed. | Same pose, horse and lightning on both screens; `ringSeq` equalled `replaySeq`. |
| Painting phase | Damage counts only for arrow, slingshot and hookshot flags (`0x1F8A4`) while the body collider is exposed. A mirror's hit is forwarded and the host applies it (health -2, `hitTimer`, invincibility), then streams the result. | Arrow hits from A and from B took 30 -> 28 -> 26 -> 24 on both clients; at 24 both switched to NEUTRAL together. |
| Neutral phase | Sword hits do damage, from either player. | 24 -> 0 with alternating swings from A and B, health identical on both at every step. |
| Defeat | On FIGHT -> DEFEATED the mirror runs `BossGanondrof_StartDefeat` (his own SetupDeath) and returns true. `OnRemoteDefeat` covers a missed edge. A client still in its intro defers the defeat. | Killing blow on the host: both clients ran the death cutscene and each got one blue warp, one heart container and the clear flag. |
| Children | Tracked: the reflected energy ball (EN_FHG_FIRE params 50). Excluded as local effects (`IsTrackingExcluded`): every other EN_FHG_FIRE (lightning, spear light, warp flashes) and the fake bosses (BOSS_GANONDROF params >= 10); each machine spawns its own from the streamed state and events. | Ball position on A followed B's. No double spawns seen. |
| Aggro | No puppet swap (the intro, the horse ride and the death read GET_PLAYER). `BossGanondrof_AimTarget` and the ball's aim feed attacks to the nearest living player on the authority. The mirror never aims; it follows the stream. | Ball and lightning aimed at the nearer player. |
| Reflected ball | The ball is authority-driven. A mirror's sword hit on the ball becomes a hit request, and the host reflects it exactly as if it had struck the ball itself. In co-op the ball has 1 hit point so a forwarded hit reflects it. | Reflected by B (host): BLUE, flew back, boss entered RETURN, ball returned. Reflected by A (mirror) with the ball in flight: B's ball turned BLUE and flew back to the boss. |
| Live test | | Intro -> painting phase -> neutral -> defeat on two clients: no desync, no crash, both players landed damage and took hits. |

Known limits:
- The ball's colour on the mirror stays green after a reflect (the mode is not streamed); the flight and the hit are correct.
- A mirror's reflect needs the ball to still be in flight when the request reaches the host; with a very short throw distance and rig latency (about 0.6 s) it can land too late. At normal arena distances it worked.
- Not exercised live: a killing blow landed by the mirror (the same forwarded-hit and edge paths as above), and a host leaving mid-fight.
- Warping Link outside the arena floor reloads the room and restarts the intro, as in the vanilla game.

## Minibosses (#4055)

Seven minibosses, each with an adapter in `BossAdapters/` (`EnTorch2`, `EnIk`, `EnZf`, `EnTest`, `EnDh` (body and hands), `EnBigokuta`, `EnFd` (dancer, core and fire ring)). Plain mirroring fell short for all of them: a suppressed mirror never runs Update, so every draw-state field, state machine and spawn that only Update drives was stale.

Changes that cover every enemy, found along the way:
- **Per-collider submit bits.** The stream now says which of AT/AC/OC the host's AI really submitted for each collider this frame (`cs` bits 3-6). The old actor-wide mask made a mirror submit every collider whose AC_ON flag was merely set, so a Stalfos or Iron Knuckle shield blocked all the time on the partner's screen, and a Lizalfos's sword hit as a hurtbox.
- **Tris streaming.** Tris collider vertices (Iron Knuckle's shield) are streamed like quads (`tv`).
- **Damage effect on replayed hits.** An adapter can set `DeriveDamageEffect`: the host looks the effect up in the damage table for the replayed flags, as a real collision check does. Dead Hand, Big Octo and Iron Knuckle drop effect-0 hits and ignored every mirror hit before.
- **Hits land on the body.** A replay prefers a non-hard hurtbox over the last collider submitted (the shield).
- **Capture order.** Collider and skeleton captures are claimed oldest-first, so a ring wrap can't swap two colliders on one client.
- **Local keys for runtime spawns every client makes.** Stalfos spawned by Bg_Mori_Bigst / En_Zl3 and the Big Octo get a key from their params and position and are never broadcast (the broadcast left an orphan beside the replica).
- **Quiet defeat.** `QuietRemoteDefeat`: a defeat the mirror plays locally never announces itself back.
- **Tracking by id for the Big Octo**, whose first fight starts as a PROP.

| Miniboss | Verdict | How, and what is left |
|---|---|---|
| Dark Link (`EN_TORCH2`) | **Adapter added** | One duel belongs to one player: the host's Link, because his AI copies that player's Player struct (sword animations, the jump onto the blade). A partner can still hit him (forwarded hits). He is out of the puppet swap and keeps the host's perception once awake; asleep, either player can wake him. Extras: action, fade-in alpha, counter state, sword-jump offset. His skeleton goes through `SkelAnime_InitLink`, which has no capture hook, so `EnTorch2_Init` announces it. Death is the generic handoff (the mirror fades him out). No finishing-blow camera on the partner. |
| Iron Knuckle, Nabooru (`EN_IK`) | **Adapter added** | Extras: animation state, armour flags (the armour pieces fly off locally from the same BodyBreak), axe-swing flag. Shield triangles stream, so mirror swings bounce off a raised shield. Nabooru's cutscenes swap `actor.update`: PRE (cutscene, runs locally), FIGHT (mirrored), DEFEATED (health 10: releases to the local fight update, which starts the defeat cutscene). Out of the puppet swap. |
| Dead Hand (`EN_DH`, `EN_DHA`) | **Adapter added** | The body starts buried, lens-only and in WAIT; extras carry action, depth, lens/target flags, dirt wave and the bite's AT toucher. Hands: arm angles, depth, action; the hand's drop is rolled on the streamed edge; a hand whose body is gone ends on resume. Bomb hits get an EXPLOSIVE stand-in attacker. Grabs go through the existing EnemyTargeting grab routing. Debris (EffectSsHahen) is not replayed on the partner. |
| Big Octo (`EN_BIGOKUTA`) | **Adapter added** | Action, both timers, platform spin rate, hand-placed colliders and the fight camera. The first fight (params 0, PROP until it starts) is now tracked from Init under a deterministic key. A hit only counts from behind: the check uses the attacking player (a partner's puppet), not the host's tracked target. Arrow hits from the partner are still judged against the host's tracked player. Death runs the Octo's own death on the mirror from the streamed pose. |
| Flare Dancer (`EN_FD`, `EN_FW`, `EN_FD_FIRE`) | **Adapter added** | Dancer: action, animation, fade, timers and the particle tick (it was invisible on a mirror). The core and the fire ring replicate as ordinary dynamic spawns with their parent. A partner's hookshot is reported to the host as an adapter event. The core's explosion plays locally on the mirror (own blast and drop). The partner's hookshot does not transfer onto the core. |
| Stalfos (`EN_TEST`) | **Adapter added** | Action and state fields, head turn, blur, ice, the shield cylinder (position streamed, submitted only while the host raises it), the Lens of Truth for the invisible one. Types 4 and 5 (the Forest Temple pair, the Ganon's tower pair) fall apart and get up again: the mirror follows the action stream, spawns the bone parts locally and hides the body meanwhile; type 5's ENEMY/PROP flip is applied. Types 0-3 die for good: health 0 hands off to the Stalfos's own fall, bone burst, drop and kill. Runtime pair spawns use local keys. |
| Lizalfos / Dinolfos (`EN_ZF`) | **Adapter added** | A lone Lizalfos and the miniboss pair start at alpha 0 and only `EnZf_DropIn` fades them in, so the mirror's copy was invisible: alpha, shadow, target flag, sword sheathing, head turn, ice and hit flash are streamed. Death: the host's `ENEMY_DIED` plays the local death (which sets the pair's clear switch for the last one). The sword quad no longer carries AC bits (`z_en_zf.c`), so the mirror stopped bouncing off it. |

### Live test (2026-10-07, two local clients, rig `tools/harness/pha4055`)

Hyrule Field and Kakariko, each miniboss spawned on the host through `anchor_test_mini("spawn:...")` (on both clients for the locally keyed ones), hit from the mirror through `anchor_test_mini("hit:...")`. Evidence is in `docs/evidence/pha4055`.

| Miniboss | Result |
|---|---|
| Lizalfos | Alpha 255 on both after the drop-in, visible on the partner's screen; mirror hits took it from 6 to 0; the partner ran its own death and both cleared. |
| Iron Knuckle | State, armour flags and axe flag identical on both at every step; mirror hits took 30 to 0 with the armour flags set at 10; death on both. |
| Dark Link | Alpha ramp and state identical; posed with sword and shield on the partner; mirror hits killed him on both. |
| Stalfos pair | Both clients keep the pair under the same keys, no orphan; a mirror hit broke one apart on both (bones flew on the partner's screen) and it got up again with 10 health on both; the last one died on both. |
| Dead Hand | Emerged and walked on both, the hand reached on the partner's screen, mirror hits killed the body and the hands vanished on both. |
| Big Octo | Same key on both, mirrored; one mirror hit from behind killed it on both. |
| Flare Dancer | Action and fade identical on both, the fire ring replicated at the same positions, the core spawned on both from a mirror hit and was killed, and the dancer defeat ended on both. |

Not exercised live: Iron Knuckle's raised shield blocking a mirror swing (the streamed state and colliders were checked, not a real swing), Nabooru's cutscenes, the Octo's first-fight platform, the partner's hookshot on the Flare Dancer, Dead Hand's grab on the partner, and a host leaving mid-fight. No desync canary, parse error or crash in either log from the adapters. A `null function` in `EnPeehat_Update` came from a test spawn of a Peehat with invalid params.

## Twinrova (#4053)

Review fixes (2026-10-09, live as `soh-web:pha4053-fix`, `soh.js?v=698a3f3a`, rollback `pre-pha4053-fix` = pha4078): a defeat deferred during a client's own merge cutscene is now started after the cutscene's SetupFly (it used to be overwritten by Fly, so no death cutscene, heart or warp); the stream cannot rewrite the action while a defeat runs; the pending-defeat flag is reset in Twinrova's Init/Destroy and keyed to the actor; the pool ring is 16 deep, expires after 60 frames, and is emptied when a client becomes host. Verified live with `anchor_test_tw(12)` (arms the deferred defeat on one client): the death cutscene, heart, warp and clear flag came on both clients. Not exercised: the real timing race (the clients are about 0.1 s apart), a host change mid-cutscene.

Live as `soh-web:pha4053` (`soh.js?v=ba384731`, FROM pha4055). Rollback tag `pre-pha4053` (= pha4055); the old container is kept stopped as `soh-web-pre-pha4053`.

Boss_Tw: Kotake (params 0), Koume (1), Twinrova (2), the fire and ice blasts (0x64, 0x66), their pools (0x65, 0x67) and the death balls (0x68, 0x69). The room is room 3 of scene 0x17; its actors exist only on the adult layer.

| Row | How | Live test (2026-10-07, two local clients) |
|---|---|---|
| Phases | One file-static stage, set where the code changes it: INTRO, WITCHES, MERGE, TWINROVA, DEFEATED. Every Boss_Tw actor reports it as its phase. The intro, the merge and the death are cutscenes with their own camera and Link, so each client runs its own. Witches mirror only while both the stream and the local stage are WITCHES; Twinrova and the blasts only in TWINROVA. A client behind in a cutscene starts the next one itself (`OnPhaseChange` / `ShouldMirror`), then joins. | Both intros ran locally (stage 0 -> 1 on both), the merge cutscene ran on both, and B then mirrored Twinrova. |
| Extras | Witches: action code, visible and hair flags, scepter, flame and portal fields, the beam (scale, state, reach, pitch, yaw, roll, reflected ray), pool values, fog and eye. Twinrova: action code, eyes, pool and flash values, timers, room light, pool and blast type, stun. Blasts: scale, tail alpha, state, type, timer, who holds the shield. The mirror runs the local half of Update itself (`BossTw_AnchorMirrorTick`: texture counters, the effect array, room light, scepter and crown sparks, the blast tail). Nothing that spawns from Draw is streamed. | Same pose, beam and pool on both screens. |
| Health | Twinrova's health streams with the pose. Witch health counts beam hits and streams too (it starts the merge). A mirror's sword hit is a hit request the host applies. | A's and B's swings each took 2 (24 -> 14 -> 4, alternating, identical on both). |
| Defeat | `BossTw_AnchorStartDefeat` (her own `SetupDeathCS`, the finishing blow, the defeat hook) on the edge, `OnRemoteDefeat` for a missed edge, deferred while the local intro or merge runs (`Anchor_TwStage`). | Killing blow from the mirror: both ran the death cutscene, each got one heart container, one blue warp and the clear flag. |
| Children | Blasts are tracked dynamic spawns. Pools are excluded and replayed from a ring in the extras (the host retracts an entry when a shield takes its pool back). Death balls are excluded: each client's own death cutscene spawns them. The witches keep room-occurrence keys (`OnEnemyActorSpawn` skips params <= 2). | One blast and one pool per client; no double spawns. |
| Aggro | No puppet swap. The beam's aim point and the blasts' launch direction use the nearest living player on the host (`Anchor_BossNearestTarget`). What a beam or blast does to a player is decided on that player's machine, because the mirror shield is about that machine's Link. A beam is a ray: every machine tests its own Link against the streamed ray (`BossTw_AnchorBeamVictim`), so a hit freezes or burns only that Link. A catch is reported (`EV_BEAM_REFLECT`), then the reflector reports its shield pose every frame (`EV_REFLECT_STATE`) and the host's reflect state follows it (`anchorReflector`). A blast that meets a mirror's shield is judged there (`OnLocalHit` -> `BossTw_AnchorBlastShield`): the charge is per player, and the verdict (absorbed, or charged and released) goes to the host (`EV_BLAST_ABSORB`). Because the verdict takes a round trip, the host holds a beam's tip and a blast at a remote player for up to 1.2 s / 0.6 s. | Koume's beam aimed at B: B caught it with the Mirror Shield, the host's witch went to reflect state with `refl` = B, Kotake (parked in front of B's shield) took the hit on both screens (health 0 -> 1, hit-by-beam). Same with A reflecting. Unshielded, B burned and A did not. B's charge went 2 -> 3, released, Twinrova was stunned on both, B's glow faded to 0 and A's charge stayed 0. |
| Resume | `BossTw_AnchorResume`: a witch goes back to flying, Twinrova to her fly state, in the stage the client is in. | Hit at each merge (the witches' phase gate); not hit with a dead stream. |
| Live test | | Intro -> witches -> beam reflected by each player -> merge -> blast, stun -> sword from both -> defeat on both clients: no desync canary and no crash in either log. Evidence: `docs/evidence/pha4053/`. Rig: `tools/harness/pha4053/`. |

Known limits:
- The reflector's stunning shot is drawn on the host (the shot's sparks are effects, not actors); the reflector's screen shows the glow and the stun, and spawns its own sparks.
- A reflect needs the verdict to reach the host within the hold (beam about 1.2 s, blast about 0.6 s); a very slow link just lets the beam or blast through.
- The shield-pose reports are about 20 events a second while a shield holds a reflect.
- `OnLocalResume` after a host change in the middle of a cutscene was not exercised.

## Ganondorf and Ganon (#4054)

Two adapters. Boss_Ganon (`GanondorfAdapter.cpp`, scene 25, entrance 0x41F) is Ganondorf, his tennis light ball, the big-magic balls and the falling platforms. Boss_Ganon2 (`Ganon2Adapter.cpp`, scene 79, entrance 0x517) is the beast in the ruins. Evidence: `docs/evidence/pha4054/` (screenshots, `logs.txt`, `adapter-log-excerpt.txt`); rig: `tools/harness/pha4054/`.

Trap found on the way: the room places Ganondorf with params 0xFFFF, which the game reads as the signed -1 (`< 0x64`). Compare params as `int16_t`, or he looks like an effect and is excluded from tracking.

| Row | How | Live test (2026-10-07, two local clients, A authority, B mirror) |
|---|---|---|
| Ganondorf phases | INTRO / FIGHT / DEFEATED from the action (`BossGanon_CoopPhase`). The intro cutscene runs on both; `ShouldMirror` waits for the local intro and the streamed FIGHT. | Both ran the intro to FIGHT; B mirrored (`sup` true) with the same pose and position. |
| Ganondorf extras | Leg sway, open hand, hand light ball, big-magic charge, the shock over him, triforce and vortex, room lighting, flashes, lens flare, targetable flag, plus a ring (`ev`) of effect spawns (params >= 0xC8) and fallen platforms that the mirror replays once. `BossGanon_CoopMirrorUpdate` runs the effect buffer and the lighting. | ring and replay counters equal on both clients (41/41). |
| Tennis ball | Tracked dynamic spawn (health forced to 1), aimed at the nearest player. A sword hit on B's copy is a hit request; A's ball is sent back at Ganondorf, who volleys it. A ball touching a Link on a mirror machine hurts that Link there and tells the authority (event 1). | B's hit reversed A's ball (mode 0 -> 1 -> 2 -> 0 in `ballMode`); B's copy followed. |
| Vulnerable and damage | A reflected big-magic ball reaching a mirror's Ganondorf is reported (event 2); the authority stuns him. Replayed hits the boss did not consume are dropped (`DropUnconsumedHits`), so damage lands only while he is vulnerable. | Stun, then a light-arrow hit from B, then sword hits from B: Wait -> HitByLightBall -> Vulnerable -> Damaged, health 3 -> 0. Before the drop flag a sword hit in Wait took 39 health through the expired-debt path. |
| Defeat | FIGHT -> DEFEATED runs `BossGanon_StartDefeat` on the mirror (his own SetupDeathCutscene, sounds, defeat hook) and returns true; a client in its intro defers it. | Both clients ran the death cutscene in lockstep and entered the tower collapse scene (26) within 0.1 s of each other. |
| Children | Excluded from tracking: the cape, the organ, the tower-collapse copy (params 1) and every params >= 0xC8 effect. | no duplicate spawns (`effects` 0 after the room load). |
| Aggro | No puppet swap. Facing, distances, the pound and the ball aim use the nearest living player (`yawTowardsPlayer` is re-pointed at the top of `BossGanon_Update`). The pound and balls hurt only the Link of the machine they run on. Limits: a partner's bottle swing does not reflect the ball; the arrow-block reflex reads the local bow. | Pound and balls followed the nearer player. |
| Ganon phases | 0 INTRO, 1 FIGHT (including him lying with Link free), 3 DOWN_CS (Zelda's text when health first drops under 21), 4 SWORD_CS (Link takes the Master Sword), 2 DEFEATED (the finale). A client mirrors only in FIGHT; a streamed edge into 3, 4 or 2 starts that cutscene locally, deferred if its own cutscene is still running. | Intro -> fight -> downed cutscene (B started its own on the edge, ran csState 1, 2, 3, then re-mirrored) -> getting up at health 25 -> finale. |
| Ganon extras | Head turn, tail sway, glows, hit flash, swing flag, look-on flag, lying flag, attention flag. `BossGanon2_CoopMirrorUpdate` runs the ring of fire and sword knock-back on the local Link, the storm and the particles. | Same pose, walk and swing on both; position within 3 units at every sample. |
| Master Sword | The intro knocks the sword out on every client. A mirror that picks it up while Ganon is lying sends event 1 and the authority starts the cutscene; at any other time the sword stays on the floor. | Event path built; the pickup itself was not driven live (the cutscene was started with a rig command). |
| Ganon damage | Hits are replayed on the authority; unconsumed ones are dropped, so a sword on his guarded front does nothing. | B's hit landed on A (`HITREQ rx`). |
| Ganon defeat | Phase 2 runs `func_80901020` on the mirror and releases it. | Both clients ran the finale (stab with the Master Sword, light, Sages) and entered the Chamber of the Sages (scene 68) within 1 s. |
| Live test | | Both fights run intro -> fight -> defeat on two clients with no crash and no desync; both players landed damage and took hits. Open: the real Master Sword pickup, the platform fall replay and the big-magic volley were not driven. |

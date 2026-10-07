#ifndef NETWORK_ANCHOR_ENEMY_TARGETING_H
#define NETWORK_ANCHOR_ENEMY_TARGETING_H

/**
 * EnemyTargeting (M3: nearest-player targeting)
 *
 * EnemySync's authority-side perception override already re-points an enemy's
 * xzDistToPlayer / yawTowardsPlayer at the nearest player, so aggro checks notice
 * a remote Link. But every enemy still ACTS on GET_PLAYER(play) — chase positions,
 * lunge targets, projectile aim, grabs — which is always the local (authority)
 * Link. That's the "enemies aim at the authority player" limitation.
 *
 * This module closes the gap for regular enemies:
 *
 *  1. Choose: each authority-run tracked enemy picks a sticky target among the
 *     living players (local Link + same-scene puppets). Sticky = it only switches
 *     when another player is clearly closer and a short hold has expired, and never
 *     while its current target is grabbed, so enemies don't ping-pong.
 *
 *  2. Act: when the target is a puppet, Actor_UpdateAll wraps that one enemy's
 *     update in Begin/End. For the duration of the update, the PLAYER actor-list
 *     head points at the puppet (so GET_PLAYER returns it), and play->damagePlayer /
 *     play->grabPlayer are swapped for routers that deliver the effect to the real
 *     remote player instead of the local save. The swap is scoped to a single
 *     actor->update() call and always restored in End.
 *
 *  3. Relay leftovers: freezes (ReDead scream) and knockbacks (func_8002F698) that
 *     the enemy writes onto the puppet during its update are forwarded to the
 *     remote player and cleared off the puppet so it keeps following its stream.
 *
 * Grab race: the host's enemy checks "is my target still grabbed?" every frame,
 * but the remote player's grabbed state only comes back a round trip later. A
 * short per-client latch ORs PLAYER_STATE2_GRABBED_BY_ENEMY into the puppet until
 * the remote's stream confirms it (or the latch expires because the remote
 * refused the grab, e.g. it was in a cutscene).
 *
 * Excluded from the swap (they keep perception-only behavior):
 *  - every ACTORCAT_BOSS: bosses drive cameras and player cutscenes off GET_PLAYER.
 *  - enemies with global side effects on "the player": Wallmaster (respawn warp),
 *    Like-Like (deletes equipment from the save), Gerudo fighters (jail warp),
 *    Poes / Poe Sisters (item gives, one-point cutscenes), Skull Kid (items,
 *    rupees, cutscenes). Floormaster hands grab and bite through the routers
 *    like any other grabber (#4045).
 *
 * Kill switch: CVar gRemote.Anchor.EnemyTargeting = 0 falls back to the previous
 * perception-only behavior.
 */

#ifdef __cplusplus
#include <cstdint>
#include <memory> // must precede extern "C": z64.h pulls in <memory> under C++
extern "C" {
#include "z64.h"
}

namespace EnemyTargeting {

// True when the kill-switch CVar is on (default on).
bool Enabled();

// Authority-side choice for one tracked enemy this frame. Returns the chosen
// perception target (the local Link or a puppet); never NULL when a local player
// exists. When the target is a puppet and the enemy is swap-eligible, arms the
// update-time swap consumed by Anchor_EnemyTargetBegin for this same actor.
Actor* SelectAndArm(Actor* actor);

// Per-frame bookkeeping: ages hold timers and grab latches, drops a stale arm.
void PerFrameTick();

// Scene teardown / disconnect.
void Reset();

// Actor destroyed: forget its target memory.
void Forget(Actor* actor);

// The victim refused a grab (cutscene, hookshot, already escaped): let go now.
void ClearGrabLatch(uint32_t clientId);

// True while an enemy update is running against a remote player's puppet.
bool SwapActive();

} // namespace EnemyTargeting
#endif // __cplusplus

// C bridge for z_actor.c (Actor_UpdateAll wraps actor->update with these).
#ifdef __cplusplus
extern "C" {
#endif
void Anchor_EnemyTargetBegin(PlayState* play, Actor* actor);
void Anchor_EnemyTargetEnd(PlayState* play, Actor* actor);
// Engine-side guard for host-only side effects aimed at "the player" (lock-on
// camera, controller rumble) while GET_PLAYER is a remote player's puppet.
s32 Anchor_EnemyTargetSwapActive(void);
#ifdef __cplusplus
}
#endif

#endif // NETWORK_ANCHOR_ENEMY_TARGETING_H

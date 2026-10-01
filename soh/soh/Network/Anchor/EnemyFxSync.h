#ifndef NETWORK_ANCHOR_ENEMY_FX_SYNC_H
#define NETWORK_ANCHOR_ENEMY_FX_SYNC_H

/**
 * EnemyFxSync (M3: enemy sounds + particles on mirror screens)
 *
 * On a mirror, a tracked enemy's update is suppressed and replaced by the
 * authority's pose stream. Anything its AI would have emitted during update —
 * roars, footsteps, the ReDead scream, fire breath, dust, splashes — never
 * happens there. Collision sparks, death effects, and anything spawned from
 * draw callbacks already play locally on every machine, so they are not synced.
 *
 * Authority side (capture): Actor_UpdateAll brackets every actor update with
 * Anchor_EnemyTargetBegin/End. For a tracked, streamed enemy (authority, a peer
 * in scene), the bracket marks it as the "capturing" actor, and:
 *   - Audio_PlaySoundGeneral calls positioned at that actor's projectedPos
 *     (Audio_PlayActorSound2 and most direct calls) are recorded;
 *   - the deferred actor->sfx field (func_8002F8F0 family, played from draw)
 *     is read back after the update;
 *   - EffectSs_Spawn calls for a whitelist of pointer-free effect types are
 *     recorded as raw init-param bytes (positions are world space, and both
 *     sides run the same build, so the struct layout matches).
 * Captures ride on the existing per-enemy ENEMY_STATE entry ("sfx", "dsfx",
 * "fx"), so no new packet type and no extra packets.
 *
 * Mirror side (replay): ingest appends to per-key queues (a burst of stream
 * packets between two frames must not drop one-shot sounds), and the mirror
 * step replays them in the enemy's own update slot while it is suppressed.
 * Once an enemy leaves the stream (death handoff, stale), the queues are
 * dropped and the local simulation makes its own sounds again.
 *
 * Kill switch: CVar gRemote.Anchor.EnemyFxSync = 0.
 */

#ifdef __cplusplus
#include <cstdint>
#include <memory> // must precede extern "C": z64.h pulls in <memory> under C++
#include <nlohmann/json.hpp>
extern "C" {
#include "z64.h"
}

namespace EnemyFxSync {

bool Enabled();

// Update bracket (called from the z_actor.c bridge for every updating actor).
void BeginActorUpdate(Actor* actor);
void EndActorUpdate(Actor* actor);

// Authority: move this actor's captures into its ENEMY_STATE entry.
void AppendToSnapshot(Actor* actor, nlohmann::json& entry);

// Authority: drop captures that no snapshot consumed this frame.
void EndFrame();

// Mirror: queue captures from an incoming ENEMY_STATE entry.
void Ingest(uint64_t key, const nlohmann::json& entry);

// Mirror: replay queued captures on the suppressed local copy.
void Replay(Actor* actor, uint64_t key);

// Mirror: this key left the stream (death handoff / stale / despawn).
void Drop(uint64_t key);

void Reset();
void Forget(Actor* actor);

} // namespace EnemyFxSync
#endif // __cplusplus

// C bridge: capture points inside the engine.
#ifdef __cplusplus
extern "C" {
#endif
void Anchor_RecordActorSfx(u16 sfxId, Vec3f* pos);
void Anchor_RecordEffectSpawn(s32 type, s32 priority, void* initParams);
#ifdef __cplusplus
}
#endif

#endif // NETWORK_ANCHOR_ENEMY_FX_SYNC_H

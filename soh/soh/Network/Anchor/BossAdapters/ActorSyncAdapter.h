#ifndef NETWORK_ANCHOR_ACTOR_SYNC_ADAPTER_H
#define NETWORK_ANCHOR_ACTOR_SYNC_ADAPTER_H
#ifdef __cplusplus

#include <cstdint>
#include <nlohmann/json.hpp>

extern "C" {
#include "z64.h"
}

/**
 * Per-actorId extension point for enemies whose fidelity needs more than
 * pos/rot/jointTable — bosses with update-computed draw params, phase-gated
 * cutscenes, and defeat sequences. Every callback is optional (nullptr).
 */
struct ActorSyncAdapter {
    // Authority: serialize extra draw-relevant fields into the stream entry.
    void (*SerializeExtras)(Actor* actor, nlohmann::json& extras) = nullptr;
    // Mirror: apply streamed extras (each applied frame, while suppressed).
    void (*DeserializeExtras)(Actor* actor, const nlohmann::json& extras) = nullptr;
    // Authority: derive a small phase enum from actor fields (never actionFunc).
    uint8_t (*GetPhase)(Actor* actor) = nullptr;
    // Mirror: react to a streamed phase edge (final streamed pose is already
    // applied). Return true to release the actor from mirroring for good —
    // the local simulation takes over (e.g. defeat cutscene).
    bool (*OnPhaseChange)(Actor* actor, uint8_t fromPhase, uint8_t toPhase) = nullptr;
    // Mirror: gate suppression; false = run the actor locally this frame
    // (e.g. intro cutscenes must run on every client).
    bool (*ShouldMirror)(Actor* actor, uint8_t streamedPhase) = nullptr;
    // Mirror: a remote client reported this boss defeated (ENEMY_DIED) but our
    // copy never took the streamed defeat edge (stale stream, missed frame).
    // Start the local defeat sequence; no-op if it is already running.
    void (*OnRemoteDefeat)(Actor* actor) = nullptr;
    // Mirror: suppression ended without a defeat (the stream went stale, or we
    // became the authority), so the local AI resumes from the last streamed
    // pose. The boss's own action state is whatever it was when mirroring
    // began; put it back into a state that can continue from here.
    void (*OnLocalResume)(Actor* actor) = nullptr;
    // Authority: a mirror reports something its player did to this boss that
    // no collider carries (King Dodongo swallowing that player's bomb). The
    // event code is the adapter's own; see EnemySync::SendAdapterEvent.
    void (*OnRemoteEvent)(Actor* actor, uint8_t event) = nullptr;
};

namespace EnemySync {

const ActorSyncAdapter* GetAdapter(int16_t actorId);
void RegisterAdapter(int16_t actorId, const ActorSyncAdapter& adapter);
// Registers all built-in adapters (Gohma, EnGoma). Idempotent.
void RegisterBuiltInAdapters();

} // namespace EnemySync

#endif // __cplusplus
#endif // NETWORK_ANCHOR_ACTOR_SYNC_ADAPTER_H

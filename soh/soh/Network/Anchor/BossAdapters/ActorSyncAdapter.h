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
    // Authority: a remote hit is about to be replayed on this actor. Return the
    // actor it should see as the attacker (Barinade needs a boomerang to stun it).
    Actor* (*RemoteHitAttacker)(Actor* actor, uint32_t dmgFlags, Actor* attacker) = nullptr;
    // Mirror: does ENEMY_DIED for this actor mean the adapter's whole-boss
    // defeat? Null = yes. Multi-part bosses (Barinade) return false for parts,
    // whose deaths replay as ordinary kills.
    bool (*HandlesDefeat)(Actor* actor) = nullptr;
    // Mirror: runs right after the cylinder colliders are repositioned from the
    // streamed world.pos, for bosses whose Update offsets a collider by hand.
    void (*PositionCollider)(Actor* actor, Collider* col) = nullptr;
    // Authority: true = leave this frame's xzDistToPlayer / yawTowardsPlayer on the
    // local Link instead of re-pointing them at the nearest player. For enemies
    // whose whole AI also reads GET_PLAYER's struct (Dark Link copies that
    // player's sword animations), so the two can't be split between players.
    bool (*KeepLocalPerception)(Actor* actor) = nullptr;
    // Authority: a remote hit about to be replayed should also carry the damage
    // effect the actor's own damage table gives those flags (stun, fire, bomb).
    // A real collision check derives it; the synthetic hit would leave
    // colChkInfo.damageEffect at whatever it was, and enemies whose hit code
    // drops effect-0 hits (Dead Hand, Big Octo) would ignore the request.
    bool DeriveDamageEffect = false;
    // Mirror: the local defeat that OnRemoteDefeat starts must not announce itself
    // again (a miniboss's death setup runs the OnEnemyDefeat hook, which would
    // send the authority's own ENEMY_DIED straight back to everyone).
    bool QuietRemoteDefeat = false;
    // Mirror: a hit on one of the actor's colliders is about to be forwarded to the authority as a hit request.
    // Return true when the adapter dealt with it on this machine (Twinrova: a mirror shield took a blast, which
    // only the machine that owns the shield can judge); nothing is forwarded then.
    bool (*OnLocalHit)(Actor* actor) = nullptr;
    // Authority: OnRemoteEvent with the sender's payload and client id (EnemySync::SendAdapterEvent's `data`).
    void (*OnRemoteEventData)(Actor* actor, uint8_t event, const nlohmann::json& data, uint32_t fromClient) = nullptr;
    // Authority: pick which collider a forwarded remote hit lands on, for
    // bosses whose damage is read from a specific collider rather than the
    // last one that called CollisionCheck_SetAC. May also prime
    // colChkInfo.damageEffect from dmgFlags. Return nullptr to drop the hit.
    Collider* (*SelectHitCollider)(Actor* actor, uint32_t dmgFlags) = nullptr;
    // The boss's parts are spawned by its own Init, deterministically on every
    // client, but after room setup (deferred object load). Keep their room
    // occurrence keys instead of minting dynamic keys and SPAWN packets, which
    // would double-spawn the parts on the other client.
    bool staticKey = false;
    // Authority: a replayed hit the boss's own update did not consume is dropped, not forced
    // onto its health. For bosses that only take damage in some states (Ganondorf is hurt
    // only while stunned, Ganon only from behind or when stunned), where a hit that lands in
    // another state must do nothing, exactly as it does for the local player.
    bool DropUnconsumedHits = false;
};

namespace EnemySync {

const ActorSyncAdapter* GetAdapter(int16_t actorId);
void RegisterAdapter(int16_t actorId, const ActorSyncAdapter& adapter);
// Registers all built-in adapters (Gohma, EnGoma). Idempotent.
void RegisterBuiltInAdapters();

} // namespace EnemySync

#endif // __cplusplus
#endif // NETWORK_ANCHOR_ACTOR_SYNC_ADAPTER_H

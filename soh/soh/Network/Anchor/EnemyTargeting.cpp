#include "soh/Network/Anchor/EnemyTargeting.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/EnemyFxSync.h"
#include "soh/Network/Anchor/Anchor.h"
#include <libultraship/libultraship.h>

#include <unordered_map>

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

namespace EnemyTargeting {

namespace {

// Sentinel clientId meaning "the local Link" in target memory.
constexpr uint32_t LOCAL_TARGET = 0xFFFFFFFEu;

// Frames an enemy keeps a freshly chosen target before it may switch again.
constexpr uint16_t TARGET_HOLD_FRAMES = 40;

// A challenger must be this much closer (squared-distance ratio) to steal the
// target once the hold expires: 0.64 == 20% closer in straight-line distance.
constexpr f32 SWITCH_RATIO_SQ = 0.64f;

// How long the host keeps a remote player "grabbed" before its stream confirms.
// OoT logic runs at 20 Hz, so 45 frames ~= 2.25 s: well past any sane round trip.
constexpr uint16_t GRAB_LATCH_FRAMES = 60;

struct TargetMemory {
    uint32_t clientId = LOCAL_TARGET;
    uint16_t hold = 0;
};

struct ArmedSwap {
    Actor* actor = nullptr;
    uint32_t clientId = 0;
};

struct ActiveSwap {
    bool on = false;
    Actor* actor = nullptr;
    uint32_t clientId = 0;
    Player* puppet = nullptr;
    Actor* savedHead = nullptr;
    s32 (*savedGrab)(PlayState*, Player*) = nullptr;
    s32 (*savedDamage)(PlayState*, s32) = nullptr;
    s16 freezeSnapshot = 0;
    Actor* focusSnapshot = nullptr;
    Actor* autoLockSnapshot = nullptr;
};

std::unordered_map<Actor*, TargetMemory> sMemory;
std::unordered_map<uint32_t, uint16_t> sGrabLatch; // clientId -> frames left
ArmedSwap sArmed;
ActiveSwap sActive;

bool IsSwapBlocked(Actor* actor) {
    if (actor->category != ACTORCAT_ENEMY) {
        return true; // bosses: cameras + player cutscenes run off GET_PLAYER
    }
    switch (actor->id) {
        case ACTOR_EN_WALLMAS:    // grab -> Play_TriggerRespawn (would warp the host)
        case ACTOR_EN_FLOORMAS:   // never mirrored; stays fully local
        case ACTOR_EN_RR:         // Like-Like: Inventory_DeleteEquipment on the host's save
        case ACTOR_EN_GELDB:      // Gerudo fighter: jail transition + player cutscene
        case ACTOR_EN_PO_FIELD:   // Item_Give / bottle checks against the host's save
        case ACTOR_EN_POH:        // same
        case ACTOR_EN_PO_SISTERS: // one-point cutscenes
        case ACTOR_EN_SKJ:        // Skull Kid: items, rupees, player cutscenes
        // Contact damage keyed on OC2_HIT_PLAYER, which only the local Link sets: the
        // host would touch it and the remote would take the hit.
        case ACTOR_EN_ST:     // Skulltula
        case ACTOR_EN_BUBBLE: // Shabom
        // Release a grab by clearing the flag on "the player" themselves (and Moblin
        // carries the player): on a puppet the real victim would stay held. Needs a
        // RELEASE effect before these can target remote players.
        case ACTOR_EN_DH:  // Dead Hand
        case ACTOR_EN_DHA: // Dead Hand's hands
        case ACTOR_EN_MB:  // Moblin
            return true;
        default:
            return false;
    }
}

bool LocalAlive(Player* local) {
    if (local == nullptr || (local->stateFlags1 & PLAYER_STATE1_DEAD)) {
        return false;
    }
    u8 ls = Anchor::Instance != nullptr ? Anchor::Instance->myLifeState : (u8)LIFE_STATE_ALIVE;
    return ls == LIFE_STATE_ALIVE || ls == LIFE_STATE_REVIVING;
}

// Resolves a same-scene, living puppet for clientId, or nullptr. Re-resolved every
// time from the client map: puppets can be killed/respawned between frames, so a
// cached Player* could dangle (same reasoning as EnemySync's perception cache).
Player* ResolvePuppet(uint32_t clientId) {
    if (Anchor::Instance == nullptr || gPlayState == NULL) {
        return nullptr;
    }
    auto it = Anchor::Instance->clients.find(clientId);
    if (it == Anchor::Instance->clients.end()) {
        return nullptr;
    }
    AnchorClient& c = it->second;
    if (c.self || !c.online || !c.isSaveLoaded || c.sceneNum != gPlayState->sceneNum || c.player == nullptr ||
        c.player->actor.update == NULL || (c.stateFlags1 & PLAYER_STATE1_DEAD) || !IsClientAlive(c)) {
        return nullptr;
    }
    return c.player;
}

Actor* ResolveTarget(uint32_t clientId, Player* local) {
    if (clientId == LOCAL_TARGET) {
        return LocalAlive(local) ? &local->actor : nullptr;
    }
    Player* p = ResolvePuppet(clientId);
    return p != nullptr ? &p->actor : nullptr;
}

bool IsGrabbed(uint32_t clientId, Actor* target) {
    if (sGrabLatch.contains(clientId)) {
        return true;
    }
    return (((Player*)target)->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) != 0;
}

f32 DistSq(Actor* from, Actor* to) {
    f32 xz = Actor_WorldDistXZToActor(from, to);
    f32 y = Actor_HeightDiff(from, to);
    return xz * xz + y * y;
}

// --- routers installed on PlayState for the duration of one swapped update ---

s32 RouteDamage(PlayState* play, s32 damage) {
    if (sActive.on && Anchor::Instance != nullptr && damage != 0) {
        // Grab bites are tagged so a victim who refused or already escaped the grab
        // (confirmation still in flight) can drop them.
        bool grabBite = (sActive.puppet->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) != 0;
        Anchor::Instance->SendPacket_EnemyPlayerEffect(sActive.clientId, ENEMY_EFFECT_HEALTH, damage, 0, 0.0f, 0.0f,
                                                       grabBite ? 1 : 0);
    }
    // Player_InflictDamage returns 1 only when the hit killed the player; the remote
    // player's own damage path decides that on their machine.
    return 0;
}

s32 RouteGrab(PlayState* play, Player* player) {
    if (!sActive.on || Anchor::Instance == nullptr || player != sActive.puppet) {
        return false;
    }
    // Mirror func_80852F38's refusals using the puppet's streamed state.
    if ((player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_IN_CUTSCENE | PLAYER_STATE1_IN_ITEM_CS |
                                PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_HANGING_OFF_LEDGE |
                                PLAYER_STATE1_CLIMBING_LEDGE)) ||
        (player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) || player->invincibilityTimer < 0) {
        return false;
    }
    Anchor::Instance->SendPacket_EnemyPlayerEffect(sActive.clientId, ENEMY_EFFECT_GRAB, 0, 0, 0.0f, 0.0f, 0);
    sGrabLatch[sActive.clientId] = GRAB_LATCH_FRAMES;
    // The rest of this update (and later frames, via the latch) sees the grab.
    player->stateFlags2 |= PLAYER_STATE2_GRABBED_BY_ENEMY;
    return true;
}

} // namespace

bool Enabled() {
    return CVarGetInteger(CVAR_REMOTE_ANCHOR("EnemyTargeting"), 1);
}

Actor* SelectAndArm(Actor* actor) {
    if (gPlayState == NULL) {
        return nullptr;
    }
    Player* local = GET_PLAYER(gPlayState);
    if (local == nullptr) {
        return nullptr;
    }

    // Nearest living candidate.
    uint32_t nearestId = LOCAL_TARGET;
    Actor* nearest = nullptr;
    f32 nearestSq = 0.0f;
    if (LocalAlive(local)) {
        nearest = &local->actor;
        nearestSq = DistSq(actor, nearest);
    }
    for (uint32_t cid : EnemySync::PerceptionTargets()) {
        Player* p = ResolvePuppet(cid);
        if (p == nullptr) {
            continue;
        }
        f32 sq = DistSq(actor, &p->actor);
        if (nearest == nullptr || sq < nearestSq) {
            nearest = &p->actor;
            nearestSq = sq;
            nearestId = cid;
        }
    }

    if (nearest == nullptr) {
        // Nobody alive in scene: leave the engine's default (local Link) alone.
        sMemory.erase(actor);
        return &local->actor;
    }

    // Sticky choice.
    TargetMemory& mem = sMemory[actor];
    Actor* current = ResolveTarget(mem.clientId, local);
    uint32_t chosenId;
    Actor* chosen;
    if (current == nullptr) {
        chosenId = nearestId;
        chosen = nearest;
        mem.hold = TARGET_HOLD_FRAMES;
    } else if (current == nearest || IsGrabbed(mem.clientId, current) || mem.hold > 0 ||
               nearestSq >= DistSq(actor, current) * SWITCH_RATIO_SQ) {
        chosenId = mem.clientId;
        chosen = current;
    } else {
        chosenId = nearestId;
        chosen = nearest;
        mem.hold = TARGET_HOLD_FRAMES;
    }
    mem.clientId = chosenId;
    if (mem.hold > 0) {
        mem.hold--;
    }

    if (chosenId != LOCAL_TARGET && !IsSwapBlocked(actor)) {
        sArmed.actor = actor;
        sArmed.clientId = chosenId;
    }
    return chosen;
}

void PerFrameTick() {
    for (auto it = sGrabLatch.begin(); it != sGrabLatch.end();) {
        if (it->second <= 1) {
            it = sGrabLatch.erase(it);
        } else {
            it->second--;
            ++it;
        }
    }
    // An arm is only valid for the update immediately following its hook.
    sArmed = {};
    if (sActive.on) {
        // Unreachable with the Actor_UpdateAll wrapper; never leave the swap in place.
        SPDLOG_WARN("[EnemyTargeting] swap left active across frames; restoring");
        if (gPlayState != NULL) {
            gPlayState->actorCtx.actorLists[ACTORCAT_PLAYER].head = sActive.savedHead;
            gPlayState->grabPlayer = sActive.savedGrab;
            gPlayState->damagePlayer = sActive.savedDamage;
        }
        sActive = {};
    }
}

void Reset() {
    // Never touches sActive: only Anchor_EnemyTargetEnd (same call stack as Begin)
    // may restore the swapped PlayState fields.
    sMemory.clear();
    sGrabLatch.clear();
    sArmed = {};
}

void ClearGrabLatch(uint32_t clientId) {
    sGrabLatch.erase(clientId);
}

bool SwapActive() {
    return sActive.on;
}

void Forget(Actor* actor) {
    sMemory.erase(actor);
    if (sArmed.actor == actor) {
        sArmed = {};
    }
}

} // namespace EnemyTargeting

using namespace EnemyTargeting;

extern "C" void Anchor_EnemyTargetBegin(PlayState* play, Actor* actor) {
    // Same bracket drives enemy sound/effect capture (EnemyFxSync).
    EnemyFxSync::BeginActorUpdate(actor);

    if (sArmed.actor != actor) {
        return; // fast path: called for every updating actor every frame
    }
    uint32_t clientId = sArmed.clientId;
    sArmed = {};

    if (sActive.on || !Enabled() || play == NULL) {
        return;
    }
    Player* puppet = ResolvePuppet(clientId);
    if (puppet == nullptr) {
        return;
    }

    // Grab latch: until the remote's stream reports the grab, keep it asserted on
    // the puppet (DummyPlayer_Update overwrote stateFlags2 from the stream earlier
    // this frame). Once the stream agrees, the stream is authoritative again (the
    // remote player mashing free clears it and the enemy lets go naturally).
    auto latch = sGrabLatch.find(clientId);
    if (latch != sGrabLatch.end()) {
        AnchorClient& c = Anchor::Instance->clients[clientId];
        if (c.stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
            sGrabLatch.erase(latch);
        } else {
            puppet->stateFlags2 |= PLAYER_STATE2_GRABBED_BY_ENEMY;
        }
    }

    // Clean slate for the post-update diff: a puppet never runs Player_Update, so
    // nothing else consumes these fields.
    puppet->knockbackType = PLAYER_KNOCKBACK_NONE;
    puppet->knockbackDamage = 0;

    sActive.on = true;
    sActive.actor = actor;
    sActive.clientId = clientId;
    sActive.puppet = puppet;
    sActive.savedHead = play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    sActive.savedGrab = play->grabPlayer;
    sActive.savedDamage = play->damagePlayer;
    sActive.freezeSnapshot = puppet->actor.freezeTimer;
    sActive.focusSnapshot = puppet->focusActor;
    sActive.autoLockSnapshot = puppet->autoLockOnActor;

    play->actorCtx.actorLists[ACTORCAT_PLAYER].head = &puppet->actor;
    play->grabPlayer = RouteGrab;
    play->damagePlayer = RouteDamage;
}

extern "C" void Anchor_EnemyTargetEnd(PlayState* play, Actor* actor) {
    EnemyFxSync::EndActorUpdate(actor);

    if (!sActive.on || sActive.actor != actor) {
        return;
    }
    if (play->actorCtx.actorLists[ACTORCAT_PLAYER].head != &sActive.puppet->actor) {
        // Something was inserted into the PLAYER list during the update (no enemy is
        // known to do this). Restoring still wins: GET_PLAYER must be the local Link.
        SPDLOG_WARN("[EnemyTargeting] PLAYER list head changed during swapped update of actor {}", actor->id);
    }
    play->actorCtx.actorLists[ACTORCAT_PLAYER].head = sActive.savedHead;
    play->grabPlayer = sActive.savedGrab;
    play->damagePlayer = sActive.savedDamage;

    Player* puppet = sActive.puppet;
    uint32_t clientId = sActive.clientId;
    s16 freezeSnapshot = sActive.freezeSnapshot;
    // Lock-on fields an enemy may write onto "the player": Actor_Delete only clears
    // them on the real Link, so a puppet keeping them would dangle once the enemy dies.
    puppet->focusActor = sActive.focusSnapshot;
    puppet->autoLockOnActor = sActive.autoLockSnapshot;
    sActive = {};

    if (Anchor::Instance == nullptr) {
        return;
    }

    // Freeze (e.g. ReDead scream: player->actor.freezeTimer = 40). A frozen puppet
    // would also stop following its stream on the host, so undo it locally.
    if (puppet->actor.freezeTimer > freezeSnapshot) {
        Anchor::Instance->SendPacket_EnemyPlayerEffect(clientId, ENEMY_EFFECT_FREEZE, puppet->actor.freezeTimer, 0,
                                                       0.0f, 0.0f, 0);
        puppet->actor.freezeTimer = freezeSnapshot;
    }

    // Knockback written through func_8002F698 (type 1 = small, 2 = large).
    if (puppet->knockbackType != PLAYER_KNOCKBACK_NONE) {
        Anchor::Instance->SendPacket_EnemyPlayerEffect(clientId, ENEMY_EFFECT_KNOCKBACK, puppet->knockbackDamage,
                                                       puppet->knockbackRot, puppet->knockbackSpeed,
                                                       puppet->knockbackYVelocity, puppet->knockbackType);
        puppet->knockbackType = PLAYER_KNOCKBACK_NONE;
        puppet->knockbackDamage = 0;
        puppet->knockbackSpeed = 0.0f;
        puppet->knockbackYVelocity = 0.0f;
    }
}

extern "C" s32 Anchor_EnemyTargetSwapActive(void) {
    return EnemyTargeting::SwapActive() ? 1 : 0;
}

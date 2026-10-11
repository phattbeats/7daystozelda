#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Am/z_en_am.h"
extern PlayState* gPlayState;
}

/**
 * Armos (EN_AM, #4181): the statues in Dodongo's Cavern (and the Spirit Temple) that shudder awake
 * when touched or hit, then lunge at Link. Pose and health stream generically, but three things
 * the suppressed Update normally sets did not reach the mirror:
 *  - textureBlend (0 = stone, 255 = live), so a mirrored Armos stayed grey while it chased;
 *  - the sword-hitbox flag (unk_264) the Draw uses to place the attack quad, and the ice timer;
 *  - the wake trigger itself. A sleeping Armos wakes when the host's Link touches it, and a
 *    partner's Link is only a puppet there, so a partner bumping it did nothing. The mirror
 *    watches its own collider for the local Link and tells the host to wake it.
 *
 * Phase 0 = asleep (host's behaviour is DO_NOTHING), 1 = awake. ARMOS_STATUE (the push-block
 * Armos, category BG) is not tracked at all.
 */

// ArmosBehavior's AM_BEHAVIOR_DO_NOTHING lives in z_en_am.c, not the header.
static constexpr int AM_BEHAVIOR_DO_NOTHING = 3;

enum AmPhase : uint8_t {
    AM_PHASE_ASLEEP = 0,
    AM_PHASE_AWAKE = 1,
};

enum AmEvent : uint8_t {
    AM_EVENT_WAKE = 1,
};

static int Am_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static uint8_t EnAm_GetPhase(Actor* actor) {
    EnAm* am = (EnAm*)actor;
    return am->behavior == AM_BEHAVIOR_DO_NOTHING ? AM_PHASE_ASLEEP : AM_PHASE_AWAKE;
}

// Runs every mirrored frame, before the pose is applied: the one place the mirror sees its own
// collision results for a suppressed Armos.
static bool EnAm_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    EnAm* am = (EnAm*)actor;
    static uint32_t lastSend = 0;
    if (streamedPhase == AM_PHASE_ASLEEP && gPlayState != NULL &&
        (am->hurtCollider.base.ocFlags1 & OC1_HIT) && am->hurtCollider.base.oc == &GET_PLAYER(gPlayState)->actor) {
        uint32_t now = gPlayState->gameplayFrames;
        if (now - lastSend > 10) {
            lastSend = now;
            EnemySync::SendAdapterEvent(actor, AM_EVENT_WAKE);
        }
    }
    return true;
}

static void EnAm_OnRemoteEvent(Actor* actor, uint8_t event) {
    EnAm* am = (EnAm*)actor;
    // unk_258 != 0 is the wake condition EnAm_Sleep checks; only meaningful while asleep.
    if (event == AM_EVENT_WAKE && actor->params != ARMOS_STATUE && am->behavior == AM_BEHAVIOR_DO_NOTHING) {
        am->unk_258 = 1;
    }
}

static void EnAm_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnAm* am = (EnAm*)actor;
    x["tb"] = am->textureBlend;
    x["yo"] = actor->shape.yOffset;
    x["sw"] = am->unk_264;
    x["ic"] = am->iceTimer;
    x["bh"] = am->behavior;
}

static void EnAm_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnAm* am = (EnAm*)actor;
    try {
        am->textureBlend = (u8)Am_Num(x, "tb", am->textureBlend);
        actor->shape.yOffset = (f32)Am_Num(x, "yo", (int)actor->shape.yOffset);
        am->unk_264 = (s16)Am_Num(x, "sw", am->unk_264);
        am->iceTimer = (s16)Am_Num(x, "ic", am->iceTimer);
        am->behavior = Am_Num(x, "bh", am->behavior);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnAm extras parse error: {}", ex.what());
    }
}

void RegisterEnAmAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnAm_SerializeExtras;
    adapter.DeserializeExtras = EnAm_DeserializeExtras;
    adapter.GetPhase = EnAm_GetPhase;
    adapter.ShouldMirror = EnAm_ShouldMirror;
    adapter.OnRemoteEvent = EnAm_OnRemoteEvent;
    adapter.DeriveDamageEffect = true;
    EnemySync::RegisterAdapter(ACTOR_EN_AM, adapter);
}

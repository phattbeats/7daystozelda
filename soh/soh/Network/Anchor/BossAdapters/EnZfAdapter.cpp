#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Zf/z_en_zf.h"
extern PlayState* gPlayState;
}

/**
 * Lizalfos and Dinolfos (EN_ZF, #4055): the lone Lizalfos, the miniboss pair
 * (types 0 and 1, tag-teaming) and the Dinolfos.
 *
 * Pose, joints, health and the sword quad stream generically. What a suppressed
 * mirror was missing:
 *  - Visibility. A lone Lizalfos and the miniboss pair start with alpha 0 and no
 *    shadow and only EnZf_DropIn fades them in, so the mirror's copy drew fully
 *    transparent for the whole fight. Alpha, shadow alpha and the Z-target flag
 *    are streamed.
 *  - Sword sheathing, head turn, ice block and hit flash (swordSheathed, headRot,
 *    iceTimer, colour filter), which Update sets and Draw reads.
 *  - Death. The host's killing blow runs Enemy_StartFinishingBlow, so ENEMY_DIED
 *    arrives, but a synthetic lethal hit is not consumed by EnZf_UpdateDamage on a
 *    mirror (frozen action, stale miniboss tag, hit lands on the wrong collider),
 *    leaving a zombie that never clears the room. OnRemoteDefeat plays the local
 *    death instead (EnZf_SetupDie, which also sets the pair's clear switch for the
 *    last one) and rolls this client's own drop.
 * The sword quad no longer carries AC bits (z_en_zf.c): the vanilla AI never
 * submitted it as a hurtbox, so streaming its on-bits made the mirror's sword
 * bounce off it.
 */

static int Zf_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static void EnZf_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnZf* z = (EnZf*)actor;
    x["ac"] = EnZf_MirrorGetAction(z);
    x["al"] = z->alpha;
    x["sa"] = actor->shape.shadowAlpha;
    x["at"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["sw"] = z->swordSheathed;
    x["hr"] = z->headRot;
    x["ic"] = z->iceTimer;
    x["fh"] = actor->floorHeight;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
}

static void EnZf_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnZf* z = (EnZf*)actor;
    try {
        EnZf_MirrorSetAction(z, Zf_Num(x, "ac", EnZf_MirrorGetAction(z)));
        z->alpha = (u8)Zf_Num(x, "al", z->alpha);
        actor->shape.shadowAlpha = (u8)Zf_Num(x, "sa", actor->shape.shadowAlpha);
        auto at = x.find("at");
        if (at != x.end() && at->is_boolean()) {
            if (at->get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        z->swordSheathed = (s16)Zf_Num(x, "sw", z->swordSheathed);
        z->headRot = (s16)Zf_Num(x, "hr", z->headRot);
        z->iceTimer = (s16)Zf_Num(x, "ic", z->iceTimer);
        auto fh = x.find("fh");
        if (fh != x.end() && fh->is_number()) {
            actor->floorHeight = fh->get<float>();
        }
        actor->colorFilterTimer = (u8)Zf_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Zf_Num(x, "cfp", actor->colorFilterParams);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnZf extras parse error: {}", ex.what());
    }
}

static void EnZf_OnRemoteDefeat(Actor* actor) {
    if (gPlayState == NULL) {
        return;
    }
    ESYNC_LOG("[EnemySync] EnZf remote defeat (local death)");
    EnZf_MirrorBeginDeath((EnZf*)actor, gPlayState);
}

static void EnZf_OnLocalResume(Actor* actor) {
    if (gPlayState != NULL) {
        EnZf_MirrorResume((EnZf*)actor, gPlayState);
    }
}

void RegisterEnZfAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnZf_SerializeExtras;
    adapter.DeserializeExtras = EnZf_DeserializeExtras;
    adapter.OnRemoteDefeat = EnZf_OnRemoteDefeat;
    adapter.OnLocalResume = EnZf_OnLocalResume;
    adapter.DeriveDamageEffect = true;
    adapter.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_ZF, adapter);
}

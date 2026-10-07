#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Bigokuta/z_en_bigokuta.h"
extern PlayState* gPlayState;
}

/**
 * Big Octo (EN_BIGOKUTA, Jabu-Jabu's miniboss), #4055.
 *
 * Pose, joints and health stream generically. A suppressed mirror also needs:
 *  - The state machine. Draw reads actionFunc (stun flash, hurt shake, death
 *    squash) and two timers, so the action id, both timers, the spin direction and
 *    the other fields the actions read are streamed, and the mirror runs on a valid
 *    actionFunc if the stream stops.
 *  - The platform. The Octo steers its parent platform's spin rate (Bg_Bdan_Objects
 *    integrates parent.world.rot.y every frame) from Update; the rate is streamed.
 *  - The hand-placed colliders. Update puts the two body cylinders and the sphere
 *    by hand (the streamed position would centre the cylinders on the actor), so
 *    they are re-placed after the pose is applied. The fight camera is Update's too.
 *  - The first fight. The platform spawns it with params 0, which makes it a PROP
 *    until the fight starts, so it was never tracked and each client fought its own
 *    copy. It is now tracked from Init under a deterministic key (every client's
 *    platform spawns its own, so nothing is broadcast).
 *  - Death. Health 0 (the stream keeps carrying it) is the DEFEATED phase; the
 *    mirror runs the Octo's own death from the streamed pose, which ends in the
 *    clear flag, the camera and the drop on every client. A hit only counts from
 *    behind: the check now uses the attacking player, not whichever player the
 *    host's Octo is tracking, so a partner's swing is judged where they stand.
 */

static int Bo_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static float Bo_Flt(const nlohmann::json& x, const char* k, float dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<float>() : dflt;
}

static uint8_t EnBigokuta_GetPhase(Actor* actor) {
    return actor->colChkInfo.health == 0 ? 1 : 0;
}

static bool EnBigokuta_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    if (toPhase == 1 && gPlayState != NULL) {
        ESYNC_LOG("[EnemySync] EnBigokuta death handoff (local death)");
        EnBigokuta_MirrorBeginDeath((EnBigokuta*)actor, gPlayState);
        return true;
    }
    return false;
}

static void EnBigokuta_OnRemoteDefeat(Actor* actor) {
    if (gPlayState != NULL) {
        EnBigokuta_MirrorBeginDeath((EnBigokuta*)actor, gPlayState);
    }
}

static void EnBigokuta_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnBigokuta* o = (EnBigokuta*)actor;
    x["af"] = EnBigokuta_MirrorGetAction(o);
    x["u194"] = o->unk_194;
    x["u195"] = o->unk_195;
    x["t196"] = o->unk_196;
    x["t198"] = o->unk_198;
    x["u19a"] = o->unk_19A;
    x["wrx"] = actor->world.rot.x;
    x["wry"] = actor->world.rot.y;
    x["hy"] = actor->home.pos.y;
    x["att"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
    if (actor->parent != NULL) {
        x["pr"] = actor->parent->world.rot.y;
    }
}

static void EnBigokuta_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnBigokuta* o = (EnBigokuta*)actor;
    try {
        int action = Bo_Num(x, "af", -1);
        if (action >= 0) {
            EnBigokuta_MirrorApplyAction(o, action);
        }
        o->unk_194 = (s8)Bo_Num(x, "u194", o->unk_194);
        o->unk_195 = (u8)Bo_Num(x, "u195", o->unk_195);
        o->unk_196 = (s16)Bo_Num(x, "t196", o->unk_196);
        o->unk_198 = (s16)Bo_Num(x, "t198", o->unk_198);
        o->unk_19A = (s16)Bo_Num(x, "u19a", o->unk_19A);
        actor->world.rot.x = (s16)Bo_Num(x, "wrx", actor->world.rot.x);
        actor->world.rot.y = (s16)Bo_Num(x, "wry", actor->world.rot.y);
        actor->home.pos.y = Bo_Flt(x, "hy", actor->home.pos.y);
        auto att = x.find("att");
        if (att != x.end() && att->is_boolean()) {
            if (att->get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        actor->colorFilterTimer = (u8)Bo_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Bo_Num(x, "cfp", actor->colorFilterParams);
        if (actor->parent != NULL && x.contains("pr")) {
            actor->parent->world.rot.y = (s16)Bo_Num(x, "pr", actor->parent->world.rot.y);
        }
        EnBigokuta_MirrorPositionColliders(o);
        if (gPlayState != NULL) {
            EnBigokuta_MirrorCamera(o, gPlayState);
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnBigokuta extras parse error: {}", ex.what());
    }
}

// SubmitColliders recentres the cylinders on the actor after the pose is applied;
// Update puts them at their own offsets.
static void EnBigokuta_PositionCollider(Actor* actor, Collider* col) {
    EnBigokuta_MirrorPositionColliders((EnBigokuta*)actor);
}

void RegisterEnBigokutaAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnBigokuta_SerializeExtras;
    adapter.DeserializeExtras = EnBigokuta_DeserializeExtras;
    adapter.GetPhase = EnBigokuta_GetPhase;
    adapter.OnPhaseChange = EnBigokuta_OnPhaseChange;
    adapter.OnRemoteDefeat = EnBigokuta_OnRemoteDefeat;
    adapter.PositionCollider = EnBigokuta_PositionCollider;
    adapter.DeriveDamageEffect = true;
    adapter.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_BIGOKUTA, adapter);
}

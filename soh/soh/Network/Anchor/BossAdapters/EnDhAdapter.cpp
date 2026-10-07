#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Dh/z_en_dh.h"
#include "src/overlays/actors/ovl_En_Dha/z_en_dha.h"
extern PlayState* gPlayState;
}

/**
 * Dead Hand (EN_DH, the body) and its two hands (EN_DHA), PHA-4055.
 *
 * The body is invisible and unhittable on a suppressed mirror: it starts buried
 * (shape.yOffset -15000), lens-only (ACTOR_FLAG_REACT_TO_LENS) and in WAIT, and
 * only Update raises it. The hands' arms are drawn from limbAngleX/Y and
 * handAngle, which Update computes (the streamed joint table is overridden for
 * those limbs), and their sink and rise runs on shape.yOffset. Extras carry all
 * of it, plus the action, so a mirror whose stream stops resumes on a valid
 * actionFunc. Streamed actors keep the body's bite and dirt-wave AT toucher
 * (flags, damage), which the AI rewrites per attack.
 *
 * Hits: both actors drop a replayed hit unless the damage table gives its flags
 * an effect (DeriveDamageEffect). The body's bomb trigger wants an EXPLOSIVE
 * actor in collider.ac, so a mirror's bomb hit is replayed with a stand-in.
 *
 * Death: the body leaves the stream the instant it dies, so the mirror gets only
 * ENEMY_DIED; OnRemoteDefeat plays the local death (fall, fade, kill) and rolls
 * this client's drop. A hand's death is a sink and a rise, which the stream
 * carries; the hand's drop is rolled on the streamed edge. The hands' real Kill
 * has no packet (their health is 8 again), so the stale stream ends in
 * EnDha_MirrorResume, which kills a hand whose Dead Hand is gone.
 *
 * Grab: Dead Hand's hands grab through play->grabPlayer, which EnemyTargeting
 * already routes to the remote player; nothing more is needed here.
 */

static int Dh_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static float Dh_Flt(const nlohmann::json& x, const char* k, float dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<float>() : dflt;
}

static void Dh_PutToucher(nlohmann::json& x, const char* k, const ColliderInfo& info) {
    x[k] = { info.toucherFlags, info.toucher.dmgFlags, info.toucher.damage };
}

static void Dh_GetToucher(const nlohmann::json& x, const char* k, ColliderInfo& info) {
    auto it = x.find(k);
    if (it == x.end() || !it->is_array() || it->size() != 3) {
        return;
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return;
        }
    }
    // TOUCH_HIT is the collision pass's own result, not the AI's setting.
    info.toucherFlags = (u8)(((*it)[0].get<int>() & ~TOUCH_HIT) | (info.toucherFlags & TOUCH_HIT));
    info.toucher.dmgFlags = (u32)(*it)[1].get<int64_t>();
    info.toucher.damage = (u8)(*it)[2].get<int>();
}

// The body's bomb trigger accepts any collider.ac in the EXPLOSIVE category.
static Actor* EnDh_RemoteHitAttacker(Actor* actor, uint32_t dmgFlags, Actor* attacker) {
    static Actor sExplosiveStandIn;
    if (dmgFlags & DMG_EXPLOSIVE) {
        memset(&sExplosiveStandIn, 0, sizeof(sExplosiveStandIn));
        sExplosiveStandIn.category = ACTORCAT_EXPLOSIVE;
        return &sExplosiveStandIn;
    }
    return attacker;
}

static void EnDh_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnDh* d = (EnDh*)actor;
    x["ca"] = d->curAction;
    x["as"] = d->actionState;
    x["rt"] = d->retreat;
    x["tm"] = d->timer;
    x["pa"] = actor->params;
    x["dp"] = d->dirtWavePhase;
    x["dw"] = d->drawDirtWave;
    x["dsp"] = d->dirtWaveSpread;
    x["dh"] = d->dirtWaveHeight;
    x["da"] = d->dirtWaveAlpha;
    x["yo"] = actor->shape.yOffset;
    x["lens"] = (actor->flags & ACTOR_FLAG_REACT_TO_LENS) != 0;
    x["att"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["al"] = d->alpha;
    x["u258"] = d->unk_258;
    x["sa"] = actor->shape.shadowAlpha;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
    x["sy"] = actor->scale.y;
    Dh_PutToucher(x, "t1", d->collider1.info);
    Dh_PutToucher(x, "t2", d->elements[0].info);
}

static void Dh_SetFlag(Actor* actor, const nlohmann::json& x, const char* k, u32 flag) {
    auto it = x.find(k);
    if (it != x.end() && it->is_boolean()) {
        if (it->get<bool>()) {
            actor->flags |= flag;
        } else {
            actor->flags &= ~flag;
        }
    }
}

static void EnDh_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnDh* d = (EnDh*)actor;
    try {
        EnDh_MirrorApplyAction(d, Dh_Num(x, "ca", d->curAction));
        d->actionState = (u8)Dh_Num(x, "as", d->actionState);
        d->retreat = (u8)Dh_Num(x, "rt", d->retreat);
        d->timer = (s16)Dh_Num(x, "tm", d->timer);
        actor->params = (s16)Dh_Num(x, "pa", actor->params);
        d->dirtWavePhase = (s16)Dh_Num(x, "dp", d->dirtWavePhase);
        d->drawDirtWave = (u8)Dh_Num(x, "dw", d->drawDirtWave);
        d->dirtWaveSpread = Dh_Flt(x, "dsp", d->dirtWaveSpread);
        d->dirtWaveHeight = Dh_Flt(x, "dh", d->dirtWaveHeight);
        d->dirtWaveAlpha = Dh_Flt(x, "da", d->dirtWaveAlpha);
        actor->shape.yOffset = Dh_Flt(x, "yo", actor->shape.yOffset);
        Dh_SetFlag(actor, x, "lens", ACTOR_FLAG_REACT_TO_LENS);
        Dh_SetFlag(actor, x, "att", ACTOR_FLAG_ATTENTION_ENABLED);
        d->alpha = (u8)Dh_Num(x, "al", d->alpha);
        d->unk_258 = (u8)Dh_Num(x, "u258", d->unk_258);
        actor->shape.shadowAlpha = (u8)Dh_Num(x, "sa", actor->shape.shadowAlpha);
        actor->colorFilterTimer = (u8)Dh_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Dh_Num(x, "cfp", actor->colorFilterParams);
        actor->scale.y = Dh_Flt(x, "sy", actor->scale.y);
        Dh_GetToucher(x, "t1", d->collider1.info);
        Dh_GetToucher(x, "t2", d->elements[0].info);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnDh extras parse error: {}", ex.what());
    }
}

static void EnDh_OnRemoteDefeat(Actor* actor) {
    if (gPlayState != NULL) {
        ESYNC_LOG("[EnemySync] EnDh remote defeat (local death)");
        EnDh_MirrorBeginDeath((EnDh*)actor, gPlayState);
    }
}

static void EnDha_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnDha* h = (EnDha*)actor;
    x["af"] = EnDha_MirrorGetAction(h);
    x["lx0"] = h->limbAngleX[0];
    x["lx1"] = h->limbAngleX[1];
    x["ly"] = h->limbAngleY;
    x["hax"] = h->handAngle.x;
    x["hay"] = h->handAngle.y;
    x["yo"] = actor->shape.yOffset;
    x["u1c0"] = h->unk_1C0;
    x["u1cc"] = h->unk_1CC;
    x["tm"] = h->timer;
    x["at"] = h->actionTimer;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
}

static void EnDha_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnDha* h = (EnDha*)actor;
    try {
        u8 prevState = h->unk_1C0;
        EnDha_MirrorApplyAction(h, Dh_Num(x, "af", EnDha_MirrorGetAction(h)));
        h->limbAngleX[0] = (s16)Dh_Num(x, "lx0", h->limbAngleX[0]);
        h->limbAngleX[1] = (s16)Dh_Num(x, "lx1", h->limbAngleX[1]);
        h->limbAngleY = (s16)Dh_Num(x, "ly", h->limbAngleY);
        h->handAngle.x = (s16)Dh_Num(x, "hax", h->handAngle.x);
        h->handAngle.y = (s16)Dh_Num(x, "hay", h->handAngle.y);
        actor->shape.yOffset = Dh_Flt(x, "yo", actor->shape.yOffset);
        h->unk_1C0 = (u8)Dh_Num(x, "u1c0", h->unk_1C0);
        h->unk_1CC = (u8)Dh_Num(x, "u1cc", h->unk_1CC);
        h->timer = (s16)Dh_Num(x, "tm", h->timer);
        h->actionTimer = (s16)Dh_Num(x, "at", h->actionTimer);
        actor->colorFilterTimer = (u8)Dh_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Dh_Num(x, "cfp", actor->colorFilterParams);
        // A hand just died on the host (its drop rolled there): roll this client's own.
        if (prevState < 8 && h->unk_1C0 == 8 && gPlayState != NULL) {
            Item_DropCollectibleRandom(gPlayState, actor, &actor->world.pos, 0xE0);
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnDha extras parse error: {}", ex.what());
    }
}

static void EnDha_OnLocalResume(Actor* actor) {
    if (gPlayState != NULL) {
        EnDha_MirrorResume((EnDha*)actor, gPlayState);
    }
}

void RegisterEnDhAdapter() {
    ActorSyncAdapter dh;
    dh.SerializeExtras = EnDh_SerializeExtras;
    dh.DeserializeExtras = EnDh_DeserializeExtras;
    dh.OnRemoteDefeat = EnDh_OnRemoteDefeat;
    dh.RemoteHitAttacker = EnDh_RemoteHitAttacker;
    dh.DeriveDamageEffect = true;
    dh.QuietRemoteDefeat = true;
    EnemySync::RegisterAdapter(ACTOR_EN_DH, dh);

    ActorSyncAdapter dha;
    dha.SerializeExtras = EnDha_SerializeExtras;
    dha.DeserializeExtras = EnDha_DeserializeExtras;
    dha.OnLocalResume = EnDha_OnLocalResume;
    dha.DeriveDamageEffect = true;
    EnemySync::RegisterAdapter(ACTOR_EN_DHA, dha);
}

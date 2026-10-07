#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Floormas/z_en_floormas.h"
extern PlayState* gPlayState;
}

/**
 * Floormaster (EN_FLOORMAS, #4045).
 *
 * One Floormaster is three actors from the start: the big hand spawns two
 * hidden small hands in its Init and links all three into a parent/child ring.
 * Killing the big hand turns all three into small ones (the split); a small
 * hand that grabbed Link and let go becomes the merge master and the other two
 * jump into it. None of this spawns or deletes actors until the last hand dies,
 * when SetupSmWait kills all three at once. EnemySync keys the two small hands
 * off the big hand's key (LinkFloormasGroup) and only ever kills the whole ring
 * together, so a mirror never holds a parent/child pointer to a freed hand.
 *
 * Pose, health, scale and collider sizes come from generic mirroring. These
 * extras carry what the suppressed Update would have set: the action (so a
 * mirror whose stream drops resumes the same state machine on valid pointers),
 * the animation, visibility (hidden hands draw NULL), params (merge roles), the
 * invulnerable green flash, and the motion fields the action reads.
 */

static void EnFloormas_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnFloormas* f = (EnFloormas*)actor;
    x["af"] = EnFloormas_MirrorGetAction(f);
    x["an"] = EnFloormas_MirrorGetAnim(f);
    x["md"] = f->skelAnime.mode;
    x["cf"] = f->skelAnime.curFrame;
    x["ps"] = f->skelAnime.playSpeed;
    x["ef"] = f->skelAnime.endFrame;
    x["dr"] = EnFloormas_MirrorGetDraw(f);
    x["pa"] = (uint16_t)actor->params;
    x["fl"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["hd"] = f->collider.base.colType == COLTYPE_HARD;
    x["hk"] = (f->collider.info.bumperFlags & BUMP_HOOKABLE) != 0;
    x["at"] = f->actionTarget;
    x["tm"] = f->actionTimer;
    x["sm"] = f->smActionTimer;
    x["zo"] = f->zOffset;
    x["sp"] = actor->speedXZ;
    x["vy"] = actor->velocity.y;
    x["gr"] = actor->gravity;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
}

static void EnFloormas_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnFloormas* f = (EnFloormas*)actor;
    try {
        bool wasShrinking = EnFloormas_MirrorIsShrinking(f);
        EnFloormas_MirrorApply(f, x.value("af", -1), x.value("an", -1), x.value("md", (int)ANIMMODE_ONCE),
                               x.value("cf", f->skelAnime.curFrame), x.value("ps", f->skelAnime.playSpeed),
                               x.value("ef", f->skelAnime.endFrame), x.value("dr", 1));
        if (x.contains("pa")) actor->params = (s16)x["pa"].get<uint16_t>();
        if (x.contains("fl")) {
            if (x["fl"].get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        // Invulnerable (hovering, merging, grabbing): swords bounce off the
        // mirror's copy too, instead of sending hit requests the authority drops.
        if (x.contains("hd")) {
            if (x["hd"].get<bool>()) {
                f->collider.base.colType = COLTYPE_HARD;
                f->collider.base.acFlags |= AC_HARD;
            } else {
                f->collider.base.colType = COLTYPE_HIT0;
                f->collider.base.acFlags &= ~AC_HARD;
            }
        }
        if (x.contains("hk")) {
            if (x["hk"].get<bool>()) {
                f->collider.info.bumperFlags |= BUMP_HOOKABLE;
            } else {
                f->collider.info.bumperFlags &= ~BUMP_HOOKABLE;
            }
        }
        if (x.contains("at")) f->actionTarget = x["at"].get<int16_t>();
        if (x.contains("tm")) f->actionTimer = x["tm"].get<int16_t>();
        if (x.contains("sm")) f->smActionTimer = x["sm"].get<int16_t>();
        if (x.contains("zo")) f->zOffset = x["zo"].get<int16_t>();
        if (x.contains("sp")) actor->speedXZ = x["sp"].get<float>();
        if (x.contains("vy")) actor->velocity.y = x["vy"].get<float>();
        if (x.contains("gr")) actor->gravity = x["gr"].get<float>();
        if (x.contains("cft")) actor->colorFilterTimer = x["cft"].get<uint8_t>();
        if (x.contains("cfp")) actor->colorFilterParams = x["cfp"].get<uint16_t>();
        actor->world.rot.y = actor->shape.rot.y;

        // A hand just died on the authority (EnFloormas_Die -> SmShrink). Its
        // drop rolled there; roll this player's own, as every other mirrored
        // enemy's local death does.
        if (!wasShrinking && EnFloormas_MirrorIsShrinking(f) && gPlayState != NULL) {
            Item_DropCollectibleRandom(gPlayState, actor, &actor->world.pos, 0x90);
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnFloormas extras parse error: {}", ex.what());
    }
}

void RegisterEnFloormasAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnFloormas_SerializeExtras;
    adapter.DeserializeExtras = EnFloormas_DeserializeExtras;
    EnemySync::RegisterAdapter(ACTOR_EN_FLOORMAS, adapter);
}

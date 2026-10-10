#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include <unordered_map>
#include <unordered_set>
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Test/z_en_test.h"
extern PlayState* gPlayState;
}

/**
 * Stalfos (EN_TEST, #4055): the room-placed ones (types 0-3), the Forest Temple
 * fights (Bg_Mori_Bigst spawns a lone type 1, then a pair of type 5) and the
 * Ganon's-tower pair (En_Zl3, type 5).
 *
 * Pose, joints, health and the sword quad stream generically. A suppressed mirror
 * was missing:
 *  - The state machine (action, the unk_7C8/unk_7DE/timer fields the actions read), the
 *    head turn, ice timer, sword-blur state and hit flash.
 *  - The shield. It is a cylinder the AI places by hand in PostLimbDraw and submits only
 *    while it is up: EnemySync now streams which colliders the AI really submitted, and the
 *    shield's position is streamed and put back after the cylinders are re-centred.
 *  - The pair's break and re-assembly (types 4 and 5 fall apart at 0 health, wait for the
 *    bones to fly home, get up with 10 health): the action stream carries it. The mirror
 *    spawns the bone parts locally and Draw hides the body meanwhile, as on the host.
 *    Type 5 also flips between the ENEMY and PROP lists; that is applied too.
 *  - Death of types 0-3: health 0 is the DEFEATED edge; the mirror runs the Stalfos's own
 *    fall, bone burst, drop and kill from the streamed pose (and Bg_Mori_Bigst's
 *    home.rot.z count goes down on each client).
 *  - The Lens of Truth for the invisible type: shown from the local lens state.
 * Runtime spawns (Bigst's, Zl3's) are made by every client's own spawner, so they carry a
 * key derived from their params and position (EnemySync LocalKeyedKey) and are never
 * broadcast, which used to leave an orphan Stalfos next to the replica on the mirror.
 *
 * Aggro: nearest-player targeting stays generic (a Stalfos aimed at the partner just
 * doesn't react to the partner's sword swings, which the puppet doesn't carry).
 */

static int Tst_Num(const nlohmann::json& x, const char* k, int dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<int>() : dflt;
}

static float Tst_Flt(const nlohmann::json& x, const char* k, float dflt) {
    auto it = x.find(k);
    return (it != x.end() && it->is_number()) ? it->get<float>() : dflt;
}

// Where the host's shield cylinder is (it is set from the shield limb each draw).
static std::unordered_map<Actor*, Vec3s> sShieldPos;

// Drops the entries of Stalfos that are gone (killed, or left behind with their scene).
static void EnTest_PruneShieldPos() {
    std::unordered_set<Actor*> live;
    if (gPlayState != NULL) {
        for (int cat : { ACTORCAT_ENEMY, ACTORCAT_PROP }) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != NULL; a = a->next) {
                if (a->id == ACTOR_EN_TEST) {
                    live.insert(a);
                }
            }
        }
    }
    for (auto it = sShieldPos.begin(); it != sShieldPos.end();) {
        it = live.contains(it->first) ? std::next(it) : sShieldPos.erase(it);
    }
}

static uint8_t EnTest_GetPhase(Actor* actor) {
    return actor->colChkInfo.health == 0 ? 1 : 0;
}

static bool EnTest_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    // Types 0-3 die for good at 0 health; 4 and 5 fall apart and get up again.
    if (toPhase == 1 && actor->params <= STALFOS_TYPE_CEILING && actor->params >= STALFOS_TYPE_INVISIBLE &&
        gPlayState != NULL) {
        ESYNC_LOG("[EnemySync] EnTest death handoff (local fall and break)");
        EnTest_MirrorBeginDeath((EnTest*)actor, gPlayState);
        return true;
    }
    return false;
}

static void EnTest_SerializeExtras(Actor* actor, nlohmann::json& x) {
    EnTest* t = (EnTest*)actor;
    x["af"] = EnTest_MirrorGetAction(t);
    x["u7c8"] = t->unk_7C8;
    x["u7de"] = t->unk_7DE;
    x["ht"] = t->headRot.y;
    x["ice"] = t->iceTimer;
    x["lde"] = t->lastDamageEffect;
    x["mws"] = t->meleeWeaponState;
    x["pa"] = actor->params;
    x["cat"] = actor->category;
    x["tm"] = t->timer;
    x["u7e4"] = t->unk_7E4;
    x["u7ec"] = t->unk_7EC;
    x["shp"] = { t->shieldCollider.dim.pos.x, t->shieldCollider.dim.pos.y, t->shieldCollider.dim.pos.z };
    x["att"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["cft"] = actor->colorFilterTimer;
    x["cfp"] = actor->colorFilterParams;
}

static void EnTest_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    EnTest* t = (EnTest*)actor;
    if (gPlayState == NULL) {
        return;
    }
    try {
        int prevAction = EnTest_MirrorGetAction(t);
        int action = Tst_Num(x, "af", prevAction);
        EnTest_MirrorApplyAction(t, action);
        actor->params = (s16)Tst_Num(x, "pa", actor->params);
        int cat = Tst_Num(x, "cat", actor->category);
        if (cat != actor->category && (cat == ACTORCAT_ENEMY || cat == ACTORCAT_PROP)) {
            Actor_ChangeCategory(gPlayState, &gPlayState->actorCtx, actor, (u8)cat);
        }
        t->unk_7C8 = (u8)Tst_Num(x, "u7c8", t->unk_7C8);
        t->unk_7DE = (u8)Tst_Num(x, "u7de", t->unk_7DE);
        t->headRot.y = (s16)Tst_Num(x, "ht", t->headRot.y);
        t->iceTimer = (s16)Tst_Num(x, "ice", t->iceTimer);
        t->lastDamageEffect = (u8)Tst_Num(x, "lde", t->lastDamageEffect);
        t->meleeWeaponState = (s8)Tst_Num(x, "mws", t->meleeWeaponState);
        t->timer = Tst_Num(x, "tm", t->timer);
        t->unk_7E4 = Tst_Num(x, "u7e4", t->unk_7E4);
        t->unk_7EC = Tst_Flt(x, "u7ec", t->unk_7EC);
        auto att = x.find("att");
        if (att != x.end() && att->is_boolean() && actor->params != STALFOS_TYPE_INVISIBLE) {
            if (att->get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        actor->colorFilterTimer = (u8)Tst_Num(x, "cft", actor->colorFilterTimer);
        actor->colorFilterParams = (u16)Tst_Num(x, "cfp", actor->colorFilterParams);
        auto shp = x.find("shp");
        if (shp != x.end() && shp->is_array() && shp->size() == 3 && (*shp)[0].is_number() && (*shp)[1].is_number() &&
            (*shp)[2].is_number()) {
            if (sShieldPos.size() >= 16) {
                EnTest_PruneShieldPos();
            }
            sShieldPos[actor] = { (s16)(*shp)[0].get<int>(), (s16)(*shp)[1].get<int>(), (s16)(*shp)[2].get<int>() };
        }

        // The pair falls apart and gets up again (types 4 and 5).
        constexpr int ACTION_BROKEN = 21; // func_80862E6C
        if (action == ACTION_BROKEN && prevAction != ACTION_BROKEN) {
            EnTest_MirrorStartBreak(t, gPlayState);
        }
        if (action == ACTION_BROKEN) {
            EnTest_MirrorBreakStep(t, gPlayState);
        } else if (prevAction == ACTION_BROKEN) {
            EnTest_MirrorEndBreak(t);
        }
        EnTest_MirrorLens(t, gPlayState);
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[EnemySync] EnTest extras parse error: {}", ex.what());
    }
}

// SubmitColliders recentres every cylinder on the actor; the shield belongs on the
// host's shield arm.
static void EnTest_PositionCollider(Actor* actor, Collider* col) {
    EnTest* t = (EnTest*)actor;
    if (col != &t->shieldCollider.base) {
        return;
    }
    auto it = sShieldPos.find(actor);
    if (it != sShieldPos.end()) {
        t->shieldCollider.dim.pos = it->second;
    }
}

void RegisterEnTestAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = EnTest_SerializeExtras;
    adapter.DeserializeExtras = EnTest_DeserializeExtras;
    adapter.GetPhase = EnTest_GetPhase;
    adapter.OnPhaseChange = EnTest_OnPhaseChange;
    adapter.PositionCollider = EnTest_PositionCollider;
    adapter.DeriveDamageEffect = true;
    adapter.QuietRemoteDefeat = true;
    // A hit the body did not consume was blocked (shield, armour, grab pose); it must not become damage.
    adapter.DropUnconsumedHits = true;
    EnemySync::RegisterAdapter(ACTOR_EN_TEST, adapter);
}

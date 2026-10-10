#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
// Pull the C++ side of global.h in under proper linkage before the extern "C"
// overlay header includes it (BossRush.cpp pattern).
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Sst/z_boss_sst.h"

BossSst* Anchor_SstPart(s32 part);
s32 Anchor_SstIntroDone(void);
s32 Anchor_SstHandState(BossSst* hand);
void Anchor_SstSetHandState(BossSst* hand, s32 state);
s32 Anchor_SstGrabAction(BossSst* hand);
u8 Anchor_SstDrumHits(void);
int Anchor_SstStage(BossSst* part);
int Anchor_SstTimer(BossSst* part);
const char* Anchor_SstFuncName(BossSst* part);
void Anchor_SstDrumTo(u8 hits);
s32 Anchor_SstDefeatStarted(void);
void Anchor_SstDeferDefeat(void);
void Anchor_SstStartDefeat(PlayState* play);
void Anchor_SstResume(BossSst* part, PlayState* play);
void Anchor_SstMirrorTick(BossSst* part, PlayState* play, s32 grabAction);
void BossSst_HandSetInvulnerable(BossSst* self, s32 isInv);
void BossSst_HeadSetupVulnerable(BossSst* self);
extern PlayState* gPlayState;
}

/**
 * Bongo Bongo (PHA-4052), the Gohma treatment.
 *
 * One adapter serves all three actors (the head, params -1, and the two hands,
 * 0 and 1), branching on params. The hands are spawned inside the head's Init,
 * which runs after room setup (the boss object loads late), so the generic
 * spawn hook would mint dynamic keys and SPAWN packets and double-spawn them on
 * the other client. staticKey keeps their deterministic occurrence keys.
 *
 * Phases come from fields: the head's health (0 = defeated) and the file-static
 * "intro done" flag Boss_Sst sets when its own intro ends. The intro runs on
 * every client (it starts when that client's Link drops onto the drum), and
 * mirroring starts once the local intro is over and the host is fighting. The
 * hands share the head's phase.
 *
 * Extras: everything Draw reads that Update computes. Head: the lens-visibility
 * flag, the attention flag, the hit-flash colour filter, the vulnerable window
 * (cylinder damage mask and the two collider sphere bumper masks), and the
 * drum counter. Hands: the attack state, the draw offsets (handZPosMod,
 * handYRotMod), invulnerability, push flags, toucher damage and the shockwave
 * and ice-shard effect arrays. The hand trails and the lens flag are derived
 * locally (Anchor_SstMirrorTick).
 *
 * The drum: BG_SST_FLOOR is a BG actor and never tracked. Each machine has its
 * own; a mirror bumps its floor when the streamed drum counter moves, so the
 * bounce moves that client's own Link.
 *
 * Child actors: no IsTrackingExcluded entries. The hands are tracked statics,
 * the floor is a local BG actor, the shockwave and ice are streamed effect
 * arrays rather than actors, and the head shadow is part of the death
 * sequence, which runs locally. Item drops from a hurt hand are made by every
 * client from the streamed hand state (each player picks up their own).
 *
 * Aggro: the EnemyTargeting puppet swap stays off (Update moves the local
 * Link and camera in the intro and the death, grabs use play->grabPlayer, and
 * the drum bounce reads GET_PLAYER). Perception (xzDistToPlayer and friends)
 * is already the nearest player on the host. The attack aim (BossSst_AimPlayer:
 * head turn, slam approach, attack gate) uses the nearest living player from
 * Anchor_BossAimTargets. What a hit does to a player stays local: the mirror's
 * re-submitted AT colliders hit that client's Link, and Anchor_SstMirrorTick
 * plays the knockback, the grab, the crush and the swing for it, which the
 * host's code only does to its own Link.
 *
 * Damage: the head reads damage only from its cylinder, the hands from their
 * sphere set. A forwarded hit lands on the right collider (SelectHitCollider)
 * and is dropped if that collider is off or hard on the host.
 *
 * Defeat: FIGHT -> DEFEATED runs the boss's own lethal-blow code locally
 * (SetupDeath for the head, SetupDamage for the hands). A mirror still in its
 * own intro waits for it to end, then dies at once.
 */

enum BongoPhase : uint8_t {
    BONGO_PHASE_PREFIGHT = 0,
    BONGO_PHASE_FIGHT = 1,
    BONGO_PHASE_DEFEATED = 2,
};

// Local copies of the file-private HAND_* enum in z_boss_sst.c.
static constexpr int kHandDamaged = 8;
static constexpr int kHandFrozen = 9;
enum : uint8_t { BONGO_NULL = 0, BONGO_ICE = 1, BONGO_SHOCKWAVE = 2, BONGO_SHADOW = 3 };

static BossSst* BG_Part(Actor* actor) {
    return (BossSst*)actor;
}

static bool BG_IsHead(Actor* actor) {
    return actor->params == BONGO_HEAD;
}

static uint8_t BG_GetPhase(Actor* actor) {
    BossSst* head = Anchor_SstPart(-1);
    if (head == nullptr || head->actor.colChkInfo.health == 0) {
        return BONGO_PHASE_DEFEATED;
    }
    return Anchor_SstIntroDone() ? BONGO_PHASE_FIGHT : BONGO_PHASE_PREFIGHT;
}

static void BG_SerializeEffects(BossSst* b, nlohmann::json& x) {
    int n = b->effectMode == BONGO_ICE ? 18 : b->effectMode == BONGO_SHOCKWAVE ? 3 : 0;
    x["em"] = b->effectMode;
    if (n == 0) {
        return;
    }
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < n; i++) {
        BossSstEffect* e = &b->effects[i];
        arr.push_back({ e->pos.x, e->pos.y, e->pos.z, e->rot.x, e->rot.y, e->rot.z, e->scale, e->move, e->status,
                        e->alpha });
    }
    x["ef"] = arr;
}

static void BG_DeserializeEffects(BossSst* b, const nlohmann::json& x) {
    uint8_t mode = x.contains("em") ? x["em"].get<uint8_t>() : (uint8_t)BONGO_NULL;
    if (mode != b->effectMode) {
        for (int i = 0; i < 18; i++) {
            b->effects[i].epoch++;
        }
    }
    b->effectMode = mode;
    if ((mode != BONGO_ICE && mode != BONGO_SHOCKWAVE) || !x.contains("ef")) {
        return;
    }
    const auto& arr = x["ef"];
    int n = std::min<int>((int)arr.size(), mode == BONGO_ICE ? 18 : 3);
    for (int i = 0; i < n; i++) {
        const auto& v = arr[i];
        if (v.size() != 10) {
            continue;
        }
        BossSstEffect* e = &b->effects[i];
        e->pos = { v[0].get<float>(), v[1].get<float>(), v[2].get<float>() };
        e->rot = { v[3].get<int16_t>(), v[4].get<int16_t>(), v[5].get<int16_t>() };
        e->scale = v[6].get<uint16_t>();
        e->move = v[7].get<int16_t>();
        e->status = v[8].get<int16_t>();
        e->alpha = v[9].get<uint8_t>();
    }
}

static void BG_SerializeExtras(Actor* actor, nlohmann::json& x) {
    BossSst* b = BG_Part(actor);
    x["at"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    x["ct"] = actor->colorFilterTimer;
    x["cp"] = actor->colorFilterParams;
    if (BG_IsHead(actor)) {
        x["vv"] = b->actionVar;
        x["cd"] = b->colliderCyl.info.bumper.dmgFlags;
        x["b0"] = b->colliderJntSph.elements[0].info.bumperFlags;
        x["b10"] = b->colliderJntSph.elements[10].info.bumperFlags;
        x["dh"] = Anchor_SstDrumHits();
        return;
    }
    x["hs"] = Anchor_SstHandState(b);
    x["ga"] = Anchor_SstGrabAction(b);
    x["hz"] = b->handZPosMod;
    x["hy"] = b->handYRotMod;
    x["inv"] = b->colliderJntSph.base.colType == COLTYPE_HARD;
    x["np"] = (b->colliderJntSph.base.ocFlags1 & OC1_NO_PUSH) != 0;
    x["dm"] = b->colliderJntSph.elements[0].info.toucher.damage;
    BG_SerializeEffects(b, x);
}

// The hand state a mirror last saw, per hand, for the item-drop edge.
static int sLastHandState[2] = { 0, 0 };

static void BG_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    BossSst* b = BG_Part(actor);
    if (gPlayState == NULL) {
        return;
    }
    int grabAction = 0;
    try {
        if (x.contains("at")) {
            if (x["at"].get<bool>()) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
        }
        if (x.contains("ct")) actor->colorFilterTimer = x["ct"].get<int16_t>();
        if (x.contains("cp")) actor->colorFilterParams = x["cp"].get<uint16_t>();
        if (BG_IsHead(actor)) {
            if (x.contains("vv")) b->actionVar = x["vv"].get<int8_t>();
            if (x.contains("cd")) b->colliderCyl.info.bumper.dmgFlags = x["cd"].get<uint32_t>();
            if (x.contains("b0")) b->colliderJntSph.elements[0].info.bumperFlags = x["b0"].get<uint8_t>();
            if (x.contains("b10")) b->colliderJntSph.elements[10].info.bumperFlags = x["b10"].get<uint8_t>();
            if (x.contains("dh")) Anchor_SstDrumTo(x["dh"].get<uint8_t>());
        } else {
            int hs = x.contains("hs") ? x["hs"].get<int>() : 0;
            int idx = actor->params == BONGO_LEFT_HAND ? 0 : 1;
            if ((hs == kHandDamaged || hs == kHandFrozen) && sLastHandState[idx] != hs) {
                Item_DropCollectible(gPlayState, &actor->world.pos,
                                     (Rand_ZeroOne() < 0.5f) ? ITEM00_ARROWS_SMALL : ITEM00_MAGIC_SMALL);
            }
            sLastHandState[idx] = hs;
            Anchor_SstSetHandState(b, hs);
            if (x.contains("ga")) grabAction = x["ga"].get<int>();
            if (x.contains("hz")) b->handZPosMod = x["hz"].get<int16_t>();
            if (x.contains("hy")) b->handYRotMod = x["hy"].get<int16_t>();
            bool inv = x.contains("inv") && x["inv"].get<bool>();
            if (inv != (b->colliderJntSph.base.colType == COLTYPE_HARD)) {
                BossSst_HandSetInvulnerable(b, inv);
            }
            if (x.contains("np")) {
                if (x["np"].get<bool>()) {
                    b->colliderJntSph.base.ocFlags1 |= OC1_NO_PUSH;
                } else {
                    b->colliderJntSph.base.ocFlags1 &= ~OC1_NO_PUSH;
                }
            }
            if (x.contains("dm")) {
                uint8_t dm = x["dm"].get<uint8_t>();
                for (int i = 0; i < 11; i++) {
                    b->colliderJntSph.elements[i].info.toucher.damage = dm;
                }
            }
            BG_DeserializeEffects(b, x);
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[BongoSync] extras parse error: {}", ex.what());
        return;
    }
    Anchor_SstMirrorTick(b, gPlayState, grabAction);
}

static bool BG_StartOrDeferDefeat() {
    if (Anchor_SstDefeatStarted()) {
        return true;
    }
    if (!Anchor_SstIntroDone()) {
        // Our own intro owns the camera: die when it ends.
        Anchor_SstDeferDefeat();
        ESYNC_LOG("[BongoSync] defeat deferred until the local intro ends");
        return true;
    }
    Anchor_SstStartDefeat(gPlayState);
    ESYNC_LOG("[BongoSync] defeat handoff (lethal-blow code run locally)");
    return true;
}

static bool BG_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[BongoSync] part {} phase {}->{}", (int)actor->params, fromPhase, toPhase);
    if (toPhase != BONGO_PHASE_DEFEATED) {
        return false;
    }
    return BG_StartOrDeferDefeat();
}

static void BG_OnRemoteDefeat(Actor* actor) {
    if (!Anchor_SstDefeatStarted()) {
        ESYNC_LOG("[BongoSync] remote defeat (missed phase edge)");
        BG_StartOrDeferDefeat();
    }
}

static bool BG_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    return streamedPhase == BONGO_PHASE_FIGHT && Anchor_SstIntroDone() && !Anchor_SstDefeatStarted() &&
           Anchor_SstPart(-1) != nullptr && Anchor_SstPart(-1)->actor.colChkInfo.health > 0;
}

static void BG_OnLocalResume(Actor* actor) {
    if (gPlayState == NULL) {
        return;
    }
    Anchor_SstResume(BG_Part(actor), gPlayState);
    ESYNC_LOG("[BongoSync] local AI resumes (part {})", (int)actor->params);
}

static Collider* BG_SelectHitCollider(Actor* actor, uint32_t dmgFlags) {
    BossSst* b = BG_Part(actor);
    Collider* col;
    if (BG_IsHead(actor)) {
        col = &b->colliderCyl.base;
    } else {
        col = &b->colliderJntSph.base;
        if (col->colType == COLTYPE_HARD) {
            return nullptr;
        }
    }
    if (!(col->acFlags & AC_ON)) {
        return nullptr;
    }
    actor->colChkInfo.damageEffect = (dmgFlags & (DMG_ARROW_ICE | DMG_MAGIC_ICE)) ? 3 : 0;
    return col;
}

void RegisterBongoBongoAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = BG_SerializeExtras;
    adapter.DeserializeExtras = BG_DeserializeExtras;
    adapter.GetPhase = BG_GetPhase;
    adapter.OnPhaseChange = BG_OnPhaseChange;
    adapter.ShouldMirror = BG_ShouldMirror;
    adapter.OnRemoteDefeat = BG_OnRemoteDefeat;
    adapter.OnLocalResume = BG_OnLocalResume;
    adapter.SelectHitCollider = BG_SelectHitCollider;
    adapter.staticKey = true;
    EnemySync::RegisterAdapter(ACTOR_BOSS_SST, adapter);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// PHA-4052 boss rig. Reports Bongo Bongo's sync state on this client:
// cmd 0 report only; 2 land a Master Sword hit on the head cylinder the way
// the collision check would; 3 make the head vulnerable now (host); 4 set
// head health to arg (host); 5 un-clear this room; 6 land a Master Sword hit
// on hand `arg` (0 left, 1 right). Every call returns the report.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_sst(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossSst* head = Anchor_SstPart(-1);
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    j["iframes"] = player->invincibilityTimer;
    j["csAction"] = player->csAction;
    j["grabbed"] = (player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) != 0;
    j["parent"] = player->actor.parent != nullptr ? (int)player->actor.parent->params : -99;
    int hearts = 0, warps = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            hearts += a->id == ACTOR_ITEM_B_HEART && a->update != NULL;
            warps += a->id == ACTOR_DOOR_WARP1 && a->update != NULL;
        }
    }
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 5) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    }
    j["present"] = head != nullptr;
    if (head == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    Actor* actor = &head->actor;
    auto land = [&](BossSst* b, Collider* col, ColliderInfo* info) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = DMG_SLASH_MASTER;
        sToucher.toucher.damage = 2;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        col->acFlags |= AC_HIT;
        col->ac = &player->actor;
        info->bumperFlags |= BUMP_HIT;
        info->acHitInfo = &sToucher;
        info->bumper.hitPos = { (s16)b->actor.focus.pos.x, (s16)b->actor.focus.pos.y, (s16)b->actor.focus.pos.z };
        b->actor.colChkInfo.damage = 2;
    };
    if (cmd == 2) {
        land(head, &head->colliderCyl.base, &head->colliderCyl.info);
    } else if (cmd == 3 && !EnemySync::IsSuppressed(actor)) {
        BossSst_HeadSetupVulnerable(head);
    } else if (cmd == 4 && !EnemySync::IsSuppressed(actor)) {
        actor->colChkInfo.health = arg;
    } else if (cmd == 6 && (arg == 0 || arg == 1) && Anchor_SstPart(arg) != nullptr) {
        BossSst* hand = Anchor_SstPart(arg);
        land(hand, &hand->colliderJntSph.base, &hand->colliderJntSph.elements[0].info);
    }
    j["phase"] = BG_GetPhase(actor);
    j["introDone"] = Anchor_SstIntroDone();
    j["defeatStarted"] = Anchor_SstDefeatStarted();
    j["hp"] = actor->colChkInfo.health;
    j["sup"] = EnemySync::IsSuppressed(actor);
    j["dying"] = EnemySync::IsDying(actor);
    j["key"] = std::to_string(EnemySync::KeyForActor(actor));
    j["pos"] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
    j["vv"] = head->actionVar;
    j["cylOn"] = (head->colliderCyl.base.acFlags & AC_ON) != 0;
    j["cylFlags"] = head->colliderCyl.info.bumper.dmgFlags;
    j["drum"] = Anchor_SstDrumHits();
    j["effMode"] = head->effectMode;
    j["stage"] = Anchor_SstStage(head);
    j["fn"] = Anchor_SstFuncName(head);
    j["timer"] = Anchor_SstTimer(head);
    nlohmann::json hands = nlohmann::json::array();
    for (int i = 0; i < 2; i++) {
        BossSst* h = Anchor_SstPart(i);
        if (h == nullptr) {
            continue;
        }
        nlohmann::json hj;
        hj["hs"] = Anchor_SstHandState(h);
        hj["ga"] = Anchor_SstGrabAction(h);
        hj["sup"] = EnemySync::IsSuppressed(&h->actor);
        hj["dying"] = EnemySync::IsDying(&h->actor);
        hj["key"] = std::to_string(EnemySync::KeyForActor(&h->actor));
        hj["pos"] = { h->actor.world.pos.x, h->actor.world.pos.y, h->actor.world.pos.z };
        hj["acOn"] = (h->colliderJntSph.base.acFlags & AC_ON) != 0;
        hj["hard"] = h->colliderJntSph.base.colType == COLTYPE_HARD;
        hj["atOn"] = (h->colliderJntSph.base.atFlags & AT_ON) != 0;
        hj["dmg"] = h->colliderJntSph.elements[0].info.toucher.damage;
        hj["eff"] = h->effectMode;
        hj["trail"] = h->trailCount;
        hj["stage"] = Anchor_SstStage(h);
        hj["fn"] = Anchor_SstFuncName(h);
        hj["timer"] = Anchor_SstTimer(h);
        hands.push_back(hj);
    }
    j["hands"] = hands;
    out = j.dump();
    return out.c_str();
}
}
#endif

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
#include "src/overlays/actors/ovl_Boss_Va/z_boss_va.h"
#include "src/overlays/actors/ovl_En_Boom/z_en_boom.h"

void BossVa_UpdateEffects(PlayState* play);
extern PlayState* gPlayState;
}

/**
 * Barinade (#4048), the Gohma treatment.
 *
 * One actor id, many parts: the body (params -1), three supports (0-2), three
 * zappers (3-5), ten Bari jellies (6-15), three stumps (16-18) and the door
 * (19). Every part shares the file-static fight state in z_boss_va.c, and the
 * body's AI is what advances it, so the mirror streams it from the body.
 *
 * Phases come from that state: PREFIGHT until the intro cutscene hands over
 * (csState reaches BATTLE), FIGHT until the body's last phase, DEFEATED from
 * the death cutscene on. Each client runs its own intro (the cutscene IS the
 * body's Update); a part mirrors only once the local intro is over and the
 * host is fighting. On the FIGHT -> DEFEATED edge every part is released to
 * local simulation and the body starts its own death cutscene, so both clients
 * get the heart container, blue warp and clear flag natively.
 *
 * Extras (every part): the draw fields Update computes that pos/rot/joints
 * miss (shape offset, world.rot the lightning is aimed along, pulse scales,
 * glow, hit-flash, support/zapper/Bari head and neck vectors, dead/burst
 * flags). Body only: the shared statics, the live-Bari and stump masks, and
 * the body collider's damage mask (its AI flips it between "boomerang only"
 * and "anything" as it is stunned, and the mirror's collider would otherwise
 * reject the hits the host accepts). Nothing that spawns pieces from draw or
 * PostLimbDraw is streamed.
 *
 * Child actors: Baris are tracked with a key derived from their params (see
 * BarinadeKey in EnemySync.cpp); each client spawns its own, the host from the
 * AI and the mirror from the streamed mask, so nothing is broadcast and
 * nothing double-spawns. Stumps and the door are local effects
 * (IsTrackingExcluded / ShouldMirror false); the mirror cuts its own support (cut skeleton, cut action) and spawns the stump from the
 * stump mask. Effects (sparks, tumours, lightning charge) are cosmetic: the
 * mirror ticks the shared effect array so nothing freezes on screen, but does
 * not receive the host's spawns.
 *
 * Boomerang: the body distinguishes the boomerang by the attacker's actor id,
 * so a replayed boomerang hit presents a stand-in EnBoom (RemoteHitAttacker).
 *
 * Aggro: Barinade stays host-only (not in the EnemyTargeting swap). Its AI
 * reads the host's Link in places an actor-level swap cannot reach (stateFlags1
 * DAMAGED and invincibilityTimer gate the zapper charge and the chase, the body
 * turns on yawTowardsPlayer), the Baris orbit the body rather than a player,
 * and every damaging collider (body, Bari spheres, lightning quads) is mirrored
 * so each player is hurt by what they actually touch. Retargeting would also
 * make the zappers' charge and cooldown depend on whichever player is nearest
 * from frame to frame, which the shared statics cannot represent.
 */

enum BarinadePhase : uint8_t {
    VA_PHASE_PREFIGHT = 0,
    VA_PHASE_FIGHT = 1,
    VA_PHASE_DEFEATED = 2,
};

static constexpr int16_t VA_PARAM_BODY = -1;
static constexpr int16_t VA_PARAM_SUPPORT_1 = 0;
static constexpr int16_t VA_PARAM_BARI_1 = 6;
static constexpr int16_t VA_PARAM_BARI_COUNT = 10;
static constexpr int16_t VA_PARAM_STUMP_1 = 16;
static constexpr int16_t VA_PARAM_DOOR = 19;
static constexpr int ABSENT_BARI_GRACE = 25;

// The host won while our own intro cutscene was still playing (set by the phase
// edge or a remote defeat, consumed once the local intro hands over).
static bool sPendingDefeat = false;

static BossVa* FindPart(int16_t params) {
    if (gPlayState == NULL) {
        return nullptr;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; a != NULL; a = a->next) {
        if (a->id == ACTOR_BOSS_VA && a->params == params && a->update != NULL) {
            return (BossVa*)a;
        }
    }
    return nullptr;
}

static uint8_t Va_GetPhase(Actor* actor) {
    BossVaSyncState s;
    BossVa_SyncGet(&s);
    if (s.csState >= BOSSVA_SYNC_DEATH_START || s.fightPhase >= BOSSVA_SYNC_PHASE_DEATH) {
        return VA_PHASE_DEFEATED;
    }
    return s.csState < BOSSVA_SYNC_BATTLE ? VA_PHASE_PREFIGHT : VA_PHASE_FIGHT;
}

static void Va_SerializeExtras(Actor* actor, nlohmann::json& x) {
    BossVa* v = (BossVa*)actor;
    x["yo"] = actor->shape.yOffset;
    x["wr"] = { actor->world.rot.x, actor->world.rot.y, actor->world.rot.z };
    x["cf"] = { actor->colorFilterTimer, actor->colorFilterParams };
    x["a"] = { v->timer2, v->bodyGlow, v->unk_1AC, v->unk_1B0, v->unk_1F0 };
    x["f"] = { v->unk_1A0, v->unk_1A4 };
    x["fl"] = { v->onCeiling, v->burst, v->isDead, v->invincibilityTimer };
    x["hr"] = { v->headRot.x, v->headRot.y, v->headRot.z };
    x["z"] = { v->unk_1E4, v->unk_1E6, v->unk_1E8, v->unk_1EA, v->unk_1EC, v->unk_1EE };
    x["v"] = { v->zapHeadPos.x, v->zapHeadPos.y, v->zapHeadPos.z, v->zapNeckPos.x, v->zapNeckPos.y, v->zapNeckPos.z,
               v->armTip.x,     v->armTip.y,     v->armTip.z,     v->unk_1D8.x,    v->unk_1D8.y,    v->unk_1D8.z };

    if (actor->params != VA_PARAM_BODY) {
        return;
    }
    BossVaSyncState s;
    BossVa_SyncGet(&s);
    x["st"] = { s.csState,    s.fightPhase,      s.bodyState,   s.phase4HP,    s.phase2Timer,
                s.doorState,  s.phase3StopMoving, s.zapperRot.x, s.zapperRot.y, s.zapperRot.z };
    x["bb"] = std::vector<int>(s.bodyBari, s.bodyBari + 10);
    x["dmg"] = v->colliderBody.info.bumper.dmgFlags;

    int bariMask = 0;
    for (int i = 0; i < VA_PARAM_BARI_COUNT; i++) {
        if (FindPart(VA_PARAM_BARI_1 + i) != nullptr) {
            bariMask |= 1 << i;
        }
    }
    int stumpMask = 0;
    for (int i = 0; i < 3; i++) {
        if (FindPart(VA_PARAM_STUMP_1 + i) != nullptr) {
            stumpMask |= 1 << i;
        }
    }
    x["bm"] = bariMask;
    x["sm"] = stumpMask;
}

static void Va_ApplyBodyExtras(BossVa* body, const nlohmann::json& x) {
    static uint8_t sAbsent[VA_PARAM_BARI_COUNT] = {};

    if (x.contains("st") && x["st"].size() == 10) {
        const auto& st = x["st"];
        BossVaSyncState s;
        BossVa_SyncGet(&s);
        s.csState = st[0].get<int>();
        s.fightPhase = st[1].get<int>();
        s.bodyState = st[2].get<int>();
        s.phase4HP = st[3].get<int>();
        s.phase2Timer = st[4].get<int>();
        s.doorState = st[5].get<int>();
        s.phase3StopMoving = st[6].get<int>();
        s.zapperRot.x = st[7].get<int>();
        s.zapperRot.y = st[8].get<int>();
        s.zapperRot.z = st[9].get<int>();
        if (x.contains("bb") && x["bb"].size() == 10) {
            for (int i = 0; i < 10; i++) {
                s.bodyBari[i] = x["bb"][i].get<int>();
            }
        }
        // The local cutscene state must not follow the stream into a death the
        // phase edge hasn't handed over yet.
        if (s.csState >= BOSSVA_SYNC_DEATH_START) {
            s.csState = BOSSVA_SYNC_BATTLE;
        }
        if (s.fightPhase >= BOSSVA_SYNC_PHASE_DEATH) {
            s.fightPhase = BOSSVA_SYNC_PHASE_DEATH - 1;
        }
        BossVa_SyncSet(&s);
    }
    if (x.contains("dmg")) {
        body->colliderBody.info.bumper.dmgFlags = x["dmg"].get<uint32_t>();
    }

    if (x.contains("bm")) {
        int mask = x["bm"].get<int>();
        for (int i = 0; i < VA_PARAM_BARI_COUNT; i++) {
            BossVa* bari = FindPart(VA_PARAM_BARI_1 + i);
            if (mask & (1 << i)) {
                sAbsent[i] = 0;
                if (bari == nullptr) {
                    BossVa_SyncSpawnBari(body, gPlayState, VA_PARAM_BARI_1 + i);
                }
            } else if (bari != nullptr && !EnemySync::IsDying((Actor*)bari)) {
                // Gone on the host with no death for us to replay (a quiet
                // Actor_Kill): don't let a stale copy resume its own AI.
                if (++sAbsent[i] > ABSENT_BARI_GRACE) {
                    sAbsent[i] = 0;
                    Actor_Kill(&bari->actor);
                }
            } else {
                sAbsent[i] = 0;
            }
        }
    }
    if (x.contains("sm")) {
        int mask = x["sm"].get<int>();
        for (int i = 0; i < 3; i++) {
            if ((mask & (1 << i)) && FindPart(VA_PARAM_STUMP_1 + i) == nullptr) {
                if (BossVa* support = FindPart(VA_PARAM_SUPPORT_1 + i)) {
                    BossVa_SyncCutSupport(support, gPlayState);
                }
            }
        }
    }

    // BossVa_Update (the body's, suppressed here) is what ticks the shared
    // particle array; without it the last frame of sparks hangs on screen.
    BossVa_UpdateEffects(gPlayState);
}

static void Va_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    BossVa* v = (BossVa*)actor;
    try {
        if (x.contains("yo")) actor->shape.yOffset = x["yo"].get<float>();
        if (x.contains("wr") && x["wr"].size() == 3) {
            actor->world.rot.x = x["wr"][0].get<int>();
            actor->world.rot.y = x["wr"][1].get<int>();
            actor->world.rot.z = x["wr"][2].get<int>();
        }
        if (x.contains("cf") && x["cf"].size() == 2) {
            actor->colorFilterTimer = x["cf"][0].get<int>();
            actor->colorFilterParams = x["cf"][1].get<int>();
        }
        if (x.contains("a") && x["a"].size() == 5) {
            v->timer2 = x["a"][0].get<int>();
            v->bodyGlow = x["a"][1].get<int>();
            v->unk_1AC = x["a"][2].get<int>();
            v->unk_1B0 = x["a"][3].get<int>();
            v->unk_1F0 = x["a"][4].get<int>();
        }
        if (x.contains("f") && x["f"].size() == 2) {
            v->unk_1A0 = x["f"][0].get<float>();
            v->unk_1A4 = x["f"][1].get<float>();
        }
        if (x.contains("fl") && x["fl"].size() == 4) {
            v->onCeiling = x["fl"][0].get<int>();
            v->burst = x["fl"][1].get<int>();
            v->isDead = x["fl"][2].get<int>();
            v->invincibilityTimer = x["fl"][3].get<int>();
        }
        if (x.contains("hr") && x["hr"].size() == 3) {
            v->headRot.x = x["hr"][0].get<int>();
            v->headRot.y = x["hr"][1].get<int>();
            v->headRot.z = x["hr"][2].get<int>();
        }
        if (x.contains("z") && x["z"].size() == 6) {
            v->unk_1E4 = x["z"][0].get<int>();
            v->unk_1E6 = x["z"][1].get<int>();
            v->unk_1E8 = x["z"][2].get<int>();
            v->unk_1EA = x["z"][3].get<int>();
            v->unk_1EC = x["z"][4].get<int>();
            v->unk_1EE = x["z"][5].get<int>();
        }
        if (x.contains("v") && x["v"].size() == 12) {
            const auto& f = x["v"];
            v->zapHeadPos = { f[0].get<float>(), f[1].get<float>(), f[2].get<float>() };
            v->zapNeckPos = { f[3].get<float>(), f[4].get<float>(), f[5].get<float>() };
            v->armTip = { f[6].get<float>(), f[7].get<float>(), f[8].get<float>() };
            v->unk_1D8 = { f[9].get<float>(), f[10].get<float>(), f[11].get<float>() };
        }
        if (actor->params == VA_PARAM_BODY) {
            Va_ApplyBodyExtras(v, x);
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[BarinadeSync] extras parse error: {}", ex.what());
    }
}

static bool Va_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[BarinadeSync] params={} phase {}->{}", actor->params, fromPhase, toPhase);
    if (toPhase != VA_PHASE_DEFEATED) {
        return false;
    }
    BossVaSyncState s;
    BossVa_SyncGet(&s);
    if (s.csState < BOSSVA_SYNC_BATTLE) {
        // Still in our own intro: starting the death now would cut into it.
        // Join the defeat when the intro hands over (polled in ShouldMirror).
        sPendingDefeat = true;
        ESYNC_LOG("[BarinadeSync] defeat deferred until the local intro ends");
        return false;
    }
    if (actor->params == VA_PARAM_BODY) {
        BossVa_SyncStartDeath((BossVa*)actor, gPlayState);
        ESYNC_LOG("[BarinadeSync] defeat handoff (SetupBodyDeath called locally)");
    }
    return true; // every part runs the death sequence's own code locally
}

static void Va_OnRemoteDefeat(Actor* actor) {
    BossVaSyncState s;
    BossVa_SyncGet(&s);
    if (s.csState >= BOSSVA_SYNC_DEATH_START) {
        return; // already dying locally
    }
    if (s.csState < BOSSVA_SYNC_BATTLE) {
        sPendingDefeat = true;
        ESYNC_LOG("[BarinadeSync] remote defeat deferred until the local intro ends");
        return;
    }
    BossVa_SyncStartDeath((BossVa*)actor, gPlayState);
    ESYNC_LOG("[BarinadeSync] remote defeat (missed phase edge, SetupBodyDeath called locally)");
}

// The stream went stale or we became the authority mid-fight: the shared
// statics already hold the last streamed state, so the AI continues from it.
static void Va_OnLocalResume(Actor* actor) {
    ESYNC_LOG("[BarinadeSync] local AI resumes (params {})", actor->params);
    if (actor->params == VA_PARAM_BODY && !EnemySync::IsDying(actor)) {
        sPendingDefeat = false;
    }
}

static bool Va_HandlesDefeat(Actor* actor) {
    return actor->params == VA_PARAM_BODY;
}

static bool Va_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    if (actor->params >= VA_PARAM_STUMP_1) {
        return false; // stumps and the door are local
    }
    BossVaSyncState s;
    BossVa_SyncGet(&s);
    if (sPendingDefeat && actor->params == VA_PARAM_BODY) {
        if (s.csState >= BOSSVA_SYNC_DEATH_START) {
            sPendingDefeat = false;
        } else if (s.csState >= BOSSVA_SYNC_BATTLE) {
            // Local intro is over: join the defeat the partner already won.
            sPendingDefeat = false;
            BossVa_SyncStartDeath((BossVa*)actor, gPlayState);
            ESYNC_LOG("[BarinadeSync] local intro over: joining the deferred defeat");
            return false;
        }
    }
    return streamedPhase == VA_PHASE_FIGHT && s.csState >= BOSSVA_SYNC_BATTLE && s.csState < BOSSVA_SYNC_DEATH_START;
}

static Actor* Va_RemoteHitAttacker(Actor* actor, uint32_t dmgFlags, Actor* attacker) {
    if (!(dmgFlags & DMG_BOOMERANG)) {
        return attacker;
    }
    // The body reads the attacker as an EnBoom (returnTimer, moveTo): hand it
    // a full-size stand-in, never a bare Actor.
    static EnBoom sStandIn;
    memset(&sStandIn, 0, sizeof(sStandIn));
    sStandIn.actor.id = ACTOR_EN_BOOM;
    sStandIn.actor.category = ACTORCAT_MISC;
    sStandIn.actor.world.pos = sStandIn.actor.home.pos = sStandIn.actor.prevPos = actor->world.pos;
    sStandIn.actor.yawTowardsPlayer = actor->yawTowardsPlayer;
    return &sStandIn.actor;
}

void RegisterBarinadeAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = Va_SerializeExtras;
    adapter.DeserializeExtras = Va_DeserializeExtras;
    adapter.GetPhase = Va_GetPhase;
    adapter.OnPhaseChange = Va_OnPhaseChange;
    adapter.ShouldMirror = Va_ShouldMirror;
    adapter.OnRemoteDefeat = Va_OnRemoteDefeat;
    adapter.OnLocalResume = Va_OnLocalResume;
    adapter.HandlesDefeat = Va_HandlesDefeat;
    adapter.RemoteHitAttacker = Va_RemoteHitAttacker;
    EnemySync::RegisterAdapter(ACTOR_BOSS_VA, adapter);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4048 boss rig. Reports Barinade's state on this client. cmd 1: sword hit
// on part `arg` (-1 body, 0-2 supports, 6-15 Baris); 2: boomerang hit on `arg`;
// 3: (host) set fightPhase to arg; 4: (host) set phase4HP to arg; 5: un-clear
// the room. Every call returns the report.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_va(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    static EnBoom sBoom;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    int hearts = 0, warps = 0, parts = 0;
    nlohmann::json plist = nlohmann::json::array();
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            hearts += a->id == ACTOR_ITEM_B_HEART && a->update != NULL;
            warps += a->id == ACTOR_DOOR_WARP1 && a->update != NULL;
            if (a->id == ACTOR_BOSS_VA && a->update != NULL) {
                parts++;
                plist.push_back(a->params);
            }
        }
    }
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["parts"] = plist;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 5) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    }
    BossVa* body = FindPart(VA_PARAM_BODY);
    j["present"] = parts > 0;
    if (parts == 0) {
        out = j.dump();
        return out.c_str();
    }
    BossVaSyncState s;
    if (cmd == 3 || cmd == 4) {
        BossVa_SyncGet(&s);
        if (cmd == 3 && !EnemySync::IsSuppressed(&body->actor)) {
            s.fightPhase = arg;
        } else if (cmd == 4 && !EnemySync::IsSuppressed(&body->actor)) {
            s.phase4HP = arg;
        }
        BossVa_SyncSet(&s);
    }
    if ((cmd == 1 && FindPart(arg) != nullptr) || (cmd == 2 && FindPart(arg) != nullptr)) {
        BossVa* v = FindPart(arg);
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = cmd == 2 ? DMG_BOOMERANG : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = cmd == 2 ? 0 : 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        Actor* attacker = &player->actor;
        if (cmd == 2) {
            memset(&sBoom, 0, sizeof(sBoom));
            sBoom.actor.id = ACTOR_EN_BOOM;
            sBoom.actor.world.pos = v->actor.world.pos;
            sBoom.moveTo = &player->actor;
            attacker = &sBoom.actor;
        }
        if (v != nullptr) {
            ColliderInfo* info = v->actor.params == VA_PARAM_BODY ? &v->colliderBody.info : &v->colliderSph.elements[0].info;
            Collider* base = v->actor.params == VA_PARAM_BODY ? &v->colliderBody.base : &v->colliderSph.base;
            base->acFlags |= AC_HIT;
            base->ac = attacker;
            info->bumperFlags |= BUMP_HIT;
            info->acHitInfo = &sToucher;
            v->actor.colChkInfo.damage = cmd == 2 ? 0 : 1;
            v->actor.colChkInfo.damageEffect = cmd == 2 ? 1 : 0;
        }
    }
    BossVa_SyncGet(&s);
    j["cs"] = s.csState;
    j["fp"] = s.fightPhase;
    j["bs"] = s.bodyState;
    j["p4"] = s.phase4HP;
    j["p2t"] = s.phase2Timer;
    j["door"] = s.doorState;
    j["bb"] = std::vector<int>(s.bodyBari, s.bodyBari + 10);
    if (body != nullptr) {
        j["phase"] = Va_GetPhase(&body->actor);
        j["sup"] = EnemySync::IsSuppressed(&body->actor);
        j["dying"] = EnemySync::IsDying(&body->actor);
        j["key"] = std::to_string(EnemySync::KeyForActor(&body->actor));
        j["pos"] = { body->actor.world.pos.x, body->actor.world.pos.y, body->actor.world.pos.z };
        j["timer"] = body->timer;
        j["yo"] = body->actor.shape.yOffset;
    }
    out = j.dump();
    return out.c_str();
}
}
#endif

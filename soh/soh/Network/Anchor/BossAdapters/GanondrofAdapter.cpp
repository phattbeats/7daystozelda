#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Ganondrof/z_boss_ganondrof.h"
#include "src/overlays/actors/ovl_En_fHG/z_en_fhg.h"
#include "src/overlays/actors/ovl_En_Fhg_Fire/z_en_fhg_fire.h"

void BossGanondrof_Intro(BossGanondrof* self, PlayState* play);
void BossGanondrof_Paintings(BossGanondrof* self, PlayState* play);
void BossGanondrof_Neutral(BossGanondrof* self, PlayState* play);
void BossGanondrof_Throw(BossGanondrof* self, PlayState* play);
void BossGanondrof_Return(BossGanondrof* self, PlayState* play);
void BossGanondrof_Stunned(BossGanondrof* self, PlayState* play);
void BossGanondrof_Block(BossGanondrof* self, PlayState* play);
void BossGanondrof_Charge(BossGanondrof* self, PlayState* play);
void BossGanondrof_Death(BossGanondrof* self, PlayState* play);
void BossGanondrof_SetupPaintings(BossGanondrof* self);
void BossGanondrof_StartDefeat(Actor* boss, PlayState* play);
void BossGanondrof_MirrorUpdate(Actor* boss, PlayState* play, s32 action);
extern PlayState* gPlayState;
}

/**
 * Phantom Ganon (PHA-4049), the Gohma treatment. One tracked boss: BOSS_GANONDROF
 * (params 1). Its horse, EN_FHG (ACTORCAT_BG, so never tracked), is the boss's child
 * and runs the intro cutscene and the painting phase; the adapter streams the horse
 * inside the boss's extras.
 *
 * Phases come from the boss's fields, never from actionFunc: DEFEATED once deathState
 * is set, FIGHT once introOver (the horse has let go of the cutscene camera, which is
 * ~75 frames after the START_FIGHT signal), INTRO before that. The intro runs on every
 * client; a client mirrors only once its own intro is over and the streamed phase is
 * FIGHT.
 *
 * Extras (what Update computes that Draw and the room read):
 *  - flyMode, an action code (the mirror's effects and collider type follow it), the
 *    leg/arm offsets Update derives from velocity, eye brightness and alpha, the hit
 *    flash timer, the stun shock timer, the attention/hookshot flags.
 *  - The horse: position, yaw, scale, the hit-shake offsets, the warp fog colour, the
 *    turn rotation the boss draws with, whether it still draws, and its Skin joint table.
 *    EnfHG_Update does nothing on a mirrored horse (Anchor_GanondrofHorseMirrored).
 *  - The mirror runs the effects Update makes every frame (BossGanondrof_MirrorUpdate:
 *    spear glitter, stun shock, spear light).
 *
 * Child actors (decision):
 *  - EN_FHG_FIRE is excluded from tracking, except the ENERGY BALL (params 50), which
 *    is the one projectile that two players interact with. Every other fire actor is a
 *    local effect: the strike, its shock and trails, bursts, the spear light and the
 *    warp. They hurt only the Link of the machine they run on (each client has its own
 *    copy). The authority records the ones that mark a moment (spear light, strike,
 *    warp in/out, the ball's burst, the fake boss) in a small ring that rides in the
 *    boss extras; a mirror replays each new entry once, so both screens show them at
 *    the same time. The death warp is not replayed: the defeat runs locally.
 *  - The fake boss + fake horse pairs (painting decoys) are excluded too. The pair is
 *    harmless and kills itself; the replay spawns one on every client.
 *  - The ball is tracked as a dynamic spawn (its health is forced to 1 so the spawn
 *    broadcasts). A hit on the mirror's ball goes to the authority as a hit request
 *    and the authority's ball reflects it, so either player can return it.
 *
 * Aggro: the EnemyTargeting puppet swap stays off for bosses (the intro, the death
 * camera and the room read GET_PLAYER). Instead, the facing and the float target of
 * the neutral hover, the charge, the ball's aim and its burst use the nearest living
 * player (Anchor_BossNearestTarget). That is safe because each machine's shock and
 * burst collide with its own Link only, and the ball is one shared actor. Limits: a
 * partner's bottle swing does not reflect the ball (the check reads the local
 * player); the sword-reflect speed-up reads the authority's own swing animation.
 *
 * Defeat: FIGHT -> DEFEATED runs the boss's own SetupDeath locally (plus the finishing
 * blow and the defeat hook) and releases it: the cutscene, blue warp, heart container
 * and clear flag all happen on every client. A client still in its own intro waits for
 * it to end (two cutscene cameras at once would strand one) and then joins.
 */

enum GanondrofPhase : uint8_t {
    GND_PHASE_INTRO = 0,
    GND_PHASE_FIGHT = 1,
    GND_PHASE_DEFEATED = 2,
};

enum GanondrofAction : uint8_t {
    GND_ACT_OTHER = 0,
    GND_ACT_NEUTRAL = 1,
    GND_ACT_THROW = 2,
    GND_ACT_RETURN = 3,
    GND_ACT_STUNNED = 4,
    GND_ACT_BLOCK = 5,
    GND_ACT_CHARGE = 6,
};

// What a fire / fake-boss spawn looks like on the wire.
struct GndEvent {
    uint32_t seq;
    int16_t id;
    uint16_t params;
    Vec3f pos;
    Vec3s rot;
};

static constexpr int GND_RING = 8;

struct GanondrofMirror {
    Actor* boss = nullptr;
    uint32_t frame = 0;
    int16_t lastShock = 0;
    uint32_t lastSeq = 0;
    bool pendingDefeat = false;
    Actor* pendingBoss = nullptr;
    bool mirroring = false;
    // Authority side: spawn ring.
    GndEvent ring[GND_RING] = {};
    uint32_t nextSeq = 0;
    bool replaying = false;
};
static GanondrofMirror sMirror;

static BossGanondrof* GND_Boss(Actor* actor) {
    return actor->id == ACTOR_BOSS_GANONDROF ? (BossGanondrof*)actor : nullptr;
}

static EnfHG* GND_FindHorse(BossGanondrof* boss) {
    if (gPlayState == NULL) {
        return nullptr;
    }
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BG].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_EN_FHG && a->parent == &boss->actor && a->update != NULL) {
            return (EnfHG*)a;
        }
    }
    return nullptr;
}

static uint8_t GND_ActionCode(BossGanondrof* b) {
    if (b->actionFunc == BossGanondrof_Neutral) return GND_ACT_NEUTRAL;
    if (b->actionFunc == BossGanondrof_Throw) return GND_ACT_THROW;
    if (b->actionFunc == BossGanondrof_Return) return GND_ACT_RETURN;
    if (b->actionFunc == BossGanondrof_Stunned) return GND_ACT_STUNNED;
    if (b->actionFunc == BossGanondrof_Block) return GND_ACT_BLOCK;
    if (b->actionFunc == BossGanondrof_Charge) return GND_ACT_CHARGE;
    return GND_ACT_OTHER;
}

static uint8_t GND_GetPhase(Actor* actor) {
    BossGanondrof* b = GND_Boss(actor);
    if (b == nullptr) {
        return GND_PHASE_INTRO;
    }
    if (b->deathState != NOT_DEAD) {
        return GND_PHASE_DEFEATED;
    }
    return b->introOver ? GND_PHASE_FIGHT : GND_PHASE_INTRO;
}

static bool GND_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    BossGanondrof* b = GND_Boss(actor);
    bool mirror = b != nullptr && streamedPhase == GND_PHASE_FIGHT && b->introOver && b->deathState == NOT_DEAD &&
                  actor->colChkInfo.health > 0;
    sMirror.mirroring = mirror;
    return mirror;
}

static void GND_PutJoints(nlohmann::json& x, const char* key, const SkelAnime& skel) {
    if (skel.jointTable == nullptr || skel.limbCount <= 0 || skel.limbCount > 40) {
        return;
    }
    std::vector<int> jt;
    jt.reserve((size_t)skel.limbCount * 3);
    for (int i = 0; i < skel.limbCount; i++) {
        jt.push_back(skel.jointTable[i].x);
        jt.push_back(skel.jointTable[i].y);
        jt.push_back(skel.jointTable[i].z);
    }
    x[key] = jt;
}

static void GND_GetJoints(const nlohmann::json& x, const char* key, SkelAnime& skel) {
    if (!x.contains(key) || skel.jointTable == nullptr) {
        return;
    }
    const auto& jt = x[key];
    if ((int)jt.size() != skel.limbCount * 3) {
        return;
    }
    for (int i = 0; i < skel.limbCount; i++) {
        skel.jointTable[i].x = jt[i * 3].get<int16_t>();
        skel.jointTable[i].y = jt[i * 3 + 1].get<int16_t>();
        skel.jointTable[i].z = jt[i * 3 + 2].get<int16_t>();
    }
}

static void GND_Serialize(Actor* actor, nlohmann::json& x) {
    BossGanondrof* b = GND_Boss(actor);
    if (b == nullptr) {
        return;
    }
    x["fm"] = b->flyMode;
    x["ac"] = GND_ActionCode(b);
    x["lr"] = { b->legRotY, b->legRotZ, b->legSplitY, b->armRotY, b->armRotZ };
    x["eb"] = { b->fwork[GND_EYE_BRIGHTNESS], b->fwork[GND_EYE_ALPHA] };
    x["iv"] = b->work[GND_INVINC_TIMER];
    x["sh"] = b->shockTimer;
    x["fl"] = ((actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) ? 1 : 0) |
              ((actor->flags & ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER) ? 2 : 0);

    nlohmann::json ev = nlohmann::json::array();
    for (int i = 0; i < GND_RING; i++) {
        const GndEvent& e = sMirror.ring[i];
        if (e.seq != 0) {
            ev.push_back({ e.seq, e.id, e.params, e.pos.x, e.pos.y, e.pos.z, e.rot.x, e.rot.y, e.rot.z });
        }
    }
    x["ev"] = ev;

    if (EnfHG* h = GND_FindHorse(b)) {
        nlohmann::json hz;
        hz["p"] = { h->actor.world.pos.x, h->actor.world.pos.y, h->actor.world.pos.z };
        hz["ry"] = h->actor.world.rot.y;
        hz["sc"] = { h->actor.scale.x, h->actor.scale.y, h->actor.scale.z };
        hz["yo"] = h->actor.shape.yOffset;
        hz["rz"] = h->actor.shape.rot.z;
        hz["wc"] = { h->warpColorFilterR, h->warpColorFilterG, h->warpColorFilterB, h->warpColorFilterUnk1,
                     h->warpColorFilterUnk2 };
        hz["tr"] = h->turnRot;
        hz["kw"] = h->fhgFireKillWarp;
        hz["dr"] = h->actor.draw != NULL;
        GND_PutJoints(hz, "jt", h->skin.skelAnime);
        x["hz"] = hz;
    }
}

static void GND_SpawnReplay(BossGanondrof* b, EnfHG* horse, const GndEvent& e) {
    Actor* parent = nullptr;
    if (e.id == ACTOR_EN_FHG_FIRE && e.params == FHGFIRE_SPEAR_LIGHT) {
        parent = &b->actor;
    } else if (e.id == ACTOR_EN_FHG_FIRE && e.params == FHGFIRE_LIGHTNING_BURST) {
        parent = nullptr;
    } else if (horse != nullptr) {
        parent = &horse->actor;
    } else {
        return;
    }
    Actor* prevChild = parent != nullptr ? parent->child : nullptr;
    sMirror.replaying = true;
    if (parent != nullptr) {
        Actor_SpawnAsChild(&gPlayState->actorCtx, parent, gPlayState, e.id, e.pos.x, e.pos.y, e.pos.z, e.rot.x,
                           e.rot.y, e.rot.z, e.params);
        parent->child = prevChild;
    } else {
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, e.id, e.pos.x, e.pos.y, e.pos.z, e.rot.x, e.rot.y, e.rot.z,
                    e.params, false);
    }
    sMirror.replaying = false;
}

static void GND_Deserialize(Actor* actor, const nlohmann::json& x) {
    BossGanondrof* b = GND_Boss(actor);
    if (b == nullptr || gPlayState == NULL) {
        return;
    }
    int action = GND_ACT_OTHER;
    try {
        uint32_t frame = gPlayState->gameplayFrames;
        bool fresh = sMirror.boss != actor || frame - sMirror.frame > 2;
        sMirror.boss = actor;
        sMirror.frame = frame;

        if (x.contains("fm")) b->flyMode = x["fm"].get<uint8_t>();
        if (x.contains("ac")) action = x["ac"].get<int>();
        if (x.contains("lr") && x["lr"].size() == 5) {
            b->legRotY = x["lr"][0].get<float>();
            b->legRotZ = x["lr"][1].get<float>();
            b->legSplitY = x["lr"][2].get<float>();
            b->armRotY = x["lr"][3].get<float>();
            b->armRotZ = x["lr"][4].get<float>();
        }
        if (x.contains("eb") && x["eb"].size() == 2) {
            b->fwork[GND_EYE_BRIGHTNESS] = x["eb"][0].get<float>();
            b->fwork[GND_EYE_ALPHA] = x["eb"][1].get<float>();
        }
        if (x.contains("iv")) b->work[GND_INVINC_TIMER] = x["iv"].get<int16_t>();
        if (x.contains("sh")) {
            int16_t streamed = x["sh"].get<int16_t>();
            if (fresh || streamed > sMirror.lastShock) {
                b->shockTimer = (u8)streamed;
            }
            sMirror.lastShock = streamed;
        }
        if (x.contains("fl")) {
            int fl = x["fl"].get<int>();
            if (fl & 1) {
                actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
            } else {
                actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
            }
            if (fl & 2) {
                actor->flags |= ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
            } else {
                actor->flags &= ~ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER;
            }
        }

        EnfHG* horse = GND_FindHorse(b);
        if (x.contains("hz") && horse != nullptr) {
            const auto& hz = x["hz"];
            if (hz.contains("p") && hz["p"].size() == 3) {
                horse->actor.world.pos.x = hz["p"][0].get<float>();
                horse->actor.world.pos.y = hz["p"][1].get<float>();
                horse->actor.world.pos.z = hz["p"][2].get<float>();
            }
            if (hz.contains("ry")) {
                horse->actor.world.rot.y = hz["ry"].get<int16_t>();
                horse->actor.shape.rot.y = horse->actor.world.rot.y;
            }
            if (hz.contains("sc") && hz["sc"].size() == 3) {
                horse->actor.scale.x = hz["sc"][0].get<float>();
                horse->actor.scale.y = hz["sc"][1].get<float>();
                horse->actor.scale.z = hz["sc"][2].get<float>();
            }
            if (hz.contains("yo")) horse->actor.shape.yOffset = hz["yo"].get<float>();
            if (hz.contains("rz")) horse->actor.shape.rot.z = hz["rz"].get<int16_t>();
            if (hz.contains("wc") && hz["wc"].size() == 5) {
                horse->warpColorFilterR = hz["wc"][0].get<float>();
                horse->warpColorFilterG = hz["wc"][1].get<float>();
                horse->warpColorFilterB = hz["wc"][2].get<float>();
                horse->warpColorFilterUnk1 = hz["wc"][3].get<float>();
                horse->warpColorFilterUnk2 = hz["wc"][4].get<float>();
            }
            if (hz.contains("tr")) horse->turnRot = hz["tr"].get<int16_t>();
            if (hz.contains("kw")) horse->fhgFireKillWarp = hz["kw"].get<uint8_t>();
            if (hz.contains("dr") && !hz["dr"].get<bool>()) horse->actor.draw = NULL;
            horse->actor.focus.pos = horse->actor.world.pos;
            horse->actor.focus.pos.y += 70.0f;
            GND_GetJoints(hz, "jt", horse->skin.skelAnime);
        }

        if (x.contains("ev") && x["ev"].is_array()) {
            uint32_t maxSeq = 0;
            for (const auto& e : x["ev"]) {
                if (e.size() == 9) {
                    maxSeq = std::max(maxSeq, e[0].get<uint32_t>());
                }
            }
            if (fresh || maxSeq < sMirror.lastSeq) {
                // Skip the backlog (and resync if the authority changed).
                sMirror.lastSeq = maxSeq;
            } else {
                for (const auto& e : x["ev"]) {
                    if (e.size() != 9 || e[0].get<uint32_t>() <= sMirror.lastSeq) {
                        continue;
                    }
                    GndEvent ev;
                    ev.seq = e[0].get<uint32_t>();
                    ev.id = e[1].get<int16_t>();
                    ev.params = e[2].get<uint16_t>();
                    ev.pos = { e[3].get<float>(), e[4].get<float>(), e[5].get<float>() };
                    ev.rot = { e[6].get<int16_t>(), e[7].get<int16_t>(), e[8].get<int16_t>() };
                    GND_SpawnReplay(b, horse, ev);
                }
                sMirror.lastSeq = maxSeq;
            }
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[GanondrofSync] extras parse error: {}", ex.what());
        return;
    }
    BossGanondrof_MirrorUpdate(actor, gPlayState, action);
}

// The body collider follows the torso (targetPos), the spear collider the spear tip. Both
// are set by hand in Update; their metal/flesh type follows the action.
static void GND_PositionCollider(Actor* actor, Collider* col) {
    BossGanondrof* b = GND_Boss(actor);
    if (b == nullptr) {
        return;
    }
    ColliderCylinder* cyl = (ColliderCylinder*)col;
    if (col == &b->colliderBody.base) {
        cyl->dim.pos.x = b->targetPos.x;
        cyl->dim.pos.y = b->targetPos.y;
        cyl->dim.pos.z = b->targetPos.z;
        uint8_t act = GND_ActionCode(b);
        b->colliderBody.base.colType = act == GND_ACT_CHARGE || act == GND_ACT_BLOCK ? COLTYPE_METAL : COLTYPE_HIT3;
    } else if (col == &b->colliderSpear.base) {
        cyl->dim.pos.x = b->spearTip.x;
        cyl->dim.pos.y = b->spearTip.y;
        cyl->dim.pos.z = b->spearTip.z;
    }
}

static bool GND_LocalIntroRunning(BossGanondrof* b) {
    return b != nullptr && !b->introOver;
}

static bool GND_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    BossGanondrof* b = GND_Boss(actor);
    ESYNC_LOG("[GanondrofSync] phase {}->{}", fromPhase, toPhase);
    if (toPhase != GND_PHASE_DEFEATED || b == nullptr || gPlayState == NULL) {
        return false;
    }
    if (GND_LocalIntroRunning(b)) {
        sMirror.pendingDefeat = true;
        sMirror.pendingBoss = actor;
        ESYNC_LOG("[GanondrofSync] defeat deferred until the local intro ends");
        return true;
    }
    BossGanondrof_StartDefeat(actor, gPlayState);
    ESYNC_LOG("[GanondrofSync] defeat handoff (death started locally)");
    return true;
}

static void GND_OnRemoteDefeat(Actor* actor) {
    BossGanondrof* b = GND_Boss(actor);
    if (b == nullptr || gPlayState == NULL || b->deathState != NOT_DEAD) {
        return;
    }
    if (GND_LocalIntroRunning(b)) {
        sMirror.pendingDefeat = true;
        sMirror.pendingBoss = actor;
        ESYNC_LOG("[GanondrofSync] remote defeat deferred until the local intro ends");
        return;
    }
    BossGanondrof_StartDefeat(actor, gPlayState);
    ESYNC_LOG("[GanondrofSync] remote defeat (missed phase edge, death started locally)");
}

// Mirroring ended without a defeat: the AI takes over from the streamed pose. The
// intro-end state is the one the boss and horse were left in, so go back to the
// painting phase, which both can continue from.
static void GND_OnLocalResume(Actor* actor) {
    BossGanondrof* b = GND_Boss(actor);
    ESYNC_LOG("[GanondrofSync] local AI resumes");
    sMirror.mirroring = false;
    if (b == nullptr || b->deathState != NOT_DEAD) {
        return;
    }
    b->flyMode = GND_FLY_PAINTING;
    BossGanondrof_SetupPaintings(b);
}

void RegisterGanondrofAdapter() {
    ActorSyncAdapter a;
    a.SerializeExtras = GND_Serialize;
    a.DeserializeExtras = GND_Deserialize;
    a.GetPhase = GND_GetPhase;
    a.OnPhaseChange = GND_OnPhaseChange;
    a.ShouldMirror = GND_ShouldMirror;
    a.OnRemoteDefeat = GND_OnRemoteDefeat;
    a.OnLocalResume = GND_OnLocalResume;
    a.PositionCollider = GND_PositionCollider;
    EnemySync::RegisterAdapter(ACTOR_BOSS_GANONDROF, a);
}

// z_boss_ganondrof.c, once the horse has let go of the cutscene camera: the partner may
// have won while our intro played.
extern "C" void Anchor_GanondrofIntroOver(Actor* bossActor) {
    if (!sMirror.pendingDefeat || sMirror.pendingBoss != bossActor || gPlayState == NULL) {
        return;
    }
    sMirror.pendingDefeat = false;
    sMirror.pendingBoss = nullptr;
    BossGanondrof_StartDefeat(bossActor, gPlayState);
    ESYNC_LOG("[GanondrofSync] local intro over: joining the deferred defeat");
}

// En_fHG: the horse of a mirrored boss only displays what the stream gives it.
extern "C" s32 Anchor_GanondrofHorseMirrored(Actor* horse) {
    return horse->params < GND_FAKE_BOSS && horse->parent != nullptr && horse->parent->id == ACTOR_BOSS_GANONDROF &&
           EnemySync::IsSuppressed(horse->parent);
}

// En_Fhg_Fire and Boss_Ganondrof Init: the authority notes the spawns a mirror should
// replay (see the file comment). Spawns made by a replay are never recorded again.
extern "C" void Anchor_GanondrofSpawned(Actor* spawned) {
    if (sMirror.replaying || !EnemySync::IsLocalAuthority() || !EnemySync::HasSameScenePeer()) {
        return;
    }
    uint16_t params = (uint16_t)spawned->params;
    bool record = false;
    if (spawned->id == ACTOR_EN_FHG_FIRE) {
        record = params == FHGFIRE_LIGHTNING_STRIKE || params == FHGFIRE_SPEAR_LIGHT || params == FHGFIRE_WARP_EMERGE ||
                 params == FHGFIRE_WARP_RETREAT || (params == FHGFIRE_LIGHTNING_BURST && spawned->home.rot.x == 200);
    } else if (spawned->id == ACTOR_BOSS_GANONDROF) {
        record = params >= GND_FAKE_BOSS;
    }
    if (!record) {
        return;
    }
    GndEvent& e = sMirror.ring[sMirror.nextSeq % GND_RING];
    e.seq = ++sMirror.nextSeq;
    e.id = spawned->id;
    e.params = params;
    e.pos = spawned->home.pos;
    e.rot = spawned->home.rot;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// PHA-4049 boss rig. Reports Phantom Ganon's sync state on this client:
// cmd 0 report only; 1 land a sword hit on the body collider the way the collision
// check would; 2 set the boss health to arg (authority); 3 un-clear this room;
// 4 teleport Link to (arg, 100, 0); 5 hit the energy ball with a sword (mirror or
// authority).
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_gnd(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossGanondrof* boss = nullptr;
    EnFhgFire* ball = nullptr;
    int fires = 0, hearts = 0, warps = 0, fakes = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->update == NULL) {
                continue;
            }
            if (a->id == ACTOR_BOSS_GANONDROF) {
                if (a->params < GND_FAKE_BOSS) {
                    boss = (BossGanondrof*)a;
                } else {
                    fakes++;
                }
            } else if (a->id == ACTOR_EN_FHG_FIRE) {
                fires++;
                if (a->params == FHGFIRE_ENERGY_BALL) {
                    ball = (EnFhgFire*)a;
                }
            }
            hearts += a->id == ACTOR_ITEM_B_HEART;
            warps += a->id == ACTOR_DOOR_WARP1;
        }
    }
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    j["iframes"] = player->invincibilityTimer;
    j["csAction"] = player->csAction;
    j["fires"] = fires;
    j["fakes"] = fakes;
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    if (cmd == 3) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    } else if (cmd == 4) {
        player->actor.world.pos.x = (float)arg;
        player->actor.world.pos.y = 100.0f;
        player->actor.world.pos.z = 0.0f;
    }
    j["present"] = boss != nullptr;
    j["ball"] = ball != nullptr;
    if (ball != nullptr) {
        j["ballPos"] = { ball->actor.world.pos.x, ball->actor.world.pos.y, ball->actor.world.pos.z };
        j["ballMode"] = ball->work[FHGFIRE_FIRE_MODE];
        j["ballSup"] = EnemySync::IsSuppressed(&ball->actor);
        if (cmd == 5) {
            memset(&sToucher, 0, sizeof(sToucher));
            sToucher.toucher.dmgFlags = DMG_SLASH_KOKIRI;
            sToucher.toucher.damage = 1;
            sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
            ball->collider.base.acFlags |= AC_HIT;
            ball->collider.base.ac = &player->actor;
            ball->collider.info.bumperFlags |= BUMP_HIT;
            ball->collider.info.acHitInfo = &sToucher;
        }
    }
    if (boss == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    j["acPre"] = (boss->colliderBody.base.acFlags & AC_HIT) != 0;
    j["inv"] = boss->work[GND_INVINC_TIMER];
    if (cmd == 1) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = arg != 0 ? (u32)arg : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        boss->colliderBody.base.acFlags |= AC_HIT;
        boss->colliderBody.base.ac = &player->actor;
        boss->colliderBody.info.bumperFlags |= BUMP_HIT;
        boss->colliderBody.info.acHitInfo = &sToucher;
    } else if (cmd == 2 && !EnemySync::IsSuppressed(&boss->actor)) {
        boss->actor.colChkInfo.health = arg;
    }
    j["phase"] = GND_GetPhase(&boss->actor);
    j["hp"] = boss->actor.colChkInfo.health;
    j["introOver"] = boss->introOver;
    j["fly"] = boss->flyMode;
    j["act"] = GND_ActionCode(boss);
    j["death"] = boss->deathState;
    j["sup"] = EnemySync::IsSuppressed(&boss->actor);
    j["dying"] = EnemySync::IsDying(&boss->actor);
    j["pos"] = { boss->actor.world.pos.x, boss->actor.world.pos.y, boss->actor.world.pos.z };
    j["pending"] = sMirror.pendingDefeat;
    j["ringSeq"] = sMirror.nextSeq;
    j["replaySeq"] = sMirror.lastSeq;
    if (EnfHG* h = GND_FindHorse(boss)) {
        j["horse"] = { h->actor.world.pos.x, h->actor.world.pos.y, h->actor.world.pos.z };
        j["horseState"] = h->cutsceneState;
        j["horseCam"] = h->cutsceneCamera;
        j["horseDraw"] = h->actor.draw != NULL;
        j["inPaint"] = h->bossGndInPainting;
        j["hitTimer"] = h->hitTimer;
    }
    out = j.dump();
    return out.c_str();
}
}
#endif

#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Ganon/z_boss_ganon.h"

u8 BossGanon_CoopPhase(Actor* thisx);
u8 BossGanon_CoopAction(Actor* thisx);
void BossGanon_CoopResume(Actor* thisx, PlayState* play);
void BossGanon_CoopHitByBall(Actor* thisx, PlayState* play);
void BossGanon_CoopBallHitPeer(Actor* ballActor, PlayState* play);
void BossGanon_CoopMirrorUpdate(Actor* thisx, PlayState* play);
void BossGanon_CoopSetColliderPos(Actor* thisx);
void BossGanon_StartDefeat(Actor* thisx, PlayState* play);
s32 BossGanon_CheckFallingPlatforms(BossGanon* dorf, PlayState* play, Vec3f* checkPos);
extern PlayState* gPlayState;
}

/**
 * Ganondorf (#4054), the Gohma treatment. Boss_Ganon is one actor id with three kinds
 * of actor, told apart by params:
 *   < 0x64     Ganondorf himself (the room places him with params 0xFFFF, which the game
 *              reads as -1; params 1 is the copy the tower-collapse cutscene uses: untracked)
 *   0x64-0xC7  the light ball he throws, the volley's tennis ball: a tracked dynamic spawn
 *   0xC8 up    effects that fly in and out of him (the charge sparks, the big-magic charge
 *              and the five big-magic balls, the flash and the thump): replayed, see below
 *
 * Phases come from the action (BossGanon_CoopPhase): the intro cutscene runs on every client
 * (Link is held by it, Zelda plays the organ); a client mirrors only once its own intro is
 * over and the streamed phase is FIGHT. The death cutscene (the collapse of the tower) runs
 * natively on every client: FIGHT -> DEFEATED starts it locally and releases him.
 *
 * Extras (what Update computes that Draw and the room read): the leg sway, the open hand,
 * the hand light ball and the big-magic charge (sizes, position, the ring of rays), the
 * shock that runs over him when hit, the triforce and vortex of the intro, the red hit
 * flash, the room lighting mode, the flash/lens-flare state, and whether he can be
 * targeted. The mirror runs BossGanon_CoopMirrorUpdate: the effects buffer and the room
 * lighting, from those fields.
 *
 * Child actors (decision):
 *  - The cape (EN_GANON_MANT) and the organ (EN_GANON_ORGAN) are spawned by each client's
 *    own Init: excluded from tracking.
 *  - The thrown light ball is the one projectile two players interact with, so it is a
 *    tracked dynamic spawn (its health is forced to 1 so the spawn broadcasts). A sword hit
 *    on the mirror's ball goes to the authority as a hit request and the authority's ball
 *    is sent back at Ganondorf, so either player can return it. A ball that touches a Link
 *    on a mirror machine is reported (event 1) and the authority ends it; the knock-back
 *    itself is applied by that machine. The ball is aimed at the nearest player.
 *  - Everything with params >= 0xC8 is a local effect, excluded from tracking. The
 *    authority records the spawns in a small ring that rides in the extras and each mirror
 *    replays a new entry once, so both screens show them together. The five big-magic balls
 *    home on the Link of the machine they run on and hurt only that Link; a ball that is
 *    sent back at the mirror's Ganondorf is reported (event 2) and the authority stuns him.
 *  - The platforms (Bg_Ganon_Otyuka) that fall when a ball or the pound lands: the authority
 *    records every one that fell and the mirror replays the check on its own platforms.
 *
 * Aggro: the EnemyTargeting puppet swap stays off for bosses (the intro, the death camera
 * and the room read GET_PLAYER). His facing, his float target, the pound and the balls' aim
 * use the nearest living player (Anchor_BossNearestTarget). That is safe because the pound
 * and the balls hurt only the Link of the machine they run on. Limits: a partner's bottle
 * swing does not reflect the ball (the check reads the local player), and the block-the-arrow
 * reflex reads the local player's bow.
 */

enum GanondorfPhase : uint8_t {
    GDF_PHASE_INTRO = 0,
    GDF_PHASE_FIGHT = 1,
    GDF_PHASE_DEFEATED = 2,
};

enum GanondorfAction : uint8_t {
    GDF_ACT_OTHER = 0,
    GDF_ACT_WAIT = 1,
    GDF_ACT_CHARGE_BALL = 2,
    GDF_ACT_TENNIS = 3,
    GDF_ACT_POUND = 4,
    GDF_ACT_BIG_MAGIC = 5,
    GDF_ACT_BLOCK = 6,
    GDF_ACT_HIT_BY_BALL = 7,
    GDF_ACT_VULNERABLE = 8,
    GDF_ACT_DAMAGED = 9,
};

enum GanondorfEvent : uint8_t {
    GDF_EVENT_BALL_HIT_LINK = 1,   // a tennis ball struck the sender's Link
    GDF_EVENT_BALL_REACHED_DORF = 2, // a reflected big-magic ball reached the sender's Ganondorf
};

static constexpr uint16_t GDF_PARAM_LIGHT_BALL_MIN = 0x64;
static constexpr uint16_t GDF_PARAM_EFFECT_MIN = 0xC8;
static constexpr int16_t GDF_PLATFORM_KIND = -1;
static constexpr int GDF_RING = 16;

struct GdfEvent {
    uint32_t seq;
    int16_t kind; // an actor id, or GDF_PLATFORM_KIND
    uint16_t params;
    Vec3f pos;
    Vec3s rot;
};

struct GanondorfMirror {
    Actor* boss = nullptr;
    uint32_t frame = 0;
    uint32_t lastSeq = 0;
    bool pendingDefeat = false;
    Actor* pendingBoss = nullptr;
    bool mirroring = false;
    GdfEvent ring[GDF_RING] = {};
    uint32_t nextSeq = 0;
    bool replaying = false;
    uint32_t ballHitFrame = 0;
};
static GanondorfMirror sMirror;

// The room's Ganondorf has params 0xFFFF; the game reads them as the signed -1 (< 0x64).
static bool GDF_IsDorf(Actor* a) {
    return a->id == ACTOR_BOSS_GANON && a->params < (int16_t)GDF_PARAM_LIGHT_BALL_MIN;
}

static bool GDF_IsBall(Actor* a) {
    return a->id == ACTOR_BOSS_GANON && a->params >= (int16_t)GDF_PARAM_LIGHT_BALL_MIN &&
           a->params < (int16_t)GDF_PARAM_EFFECT_MIN;
}

static float GDF_F(const nlohmann::json& x, const char* key, float def = 0.0f) {
    auto it = x.find(key);
    return it != x.end() && it->is_number() ? it->get<float>() : def;
}

static int GDF_I(const nlohmann::json& x, const char* key, int def = 0) {
    auto it = x.find(key);
    return it != x.end() && it->is_number() ? (int)it->get<float>() : def;
}

// The array under `key` when it has exactly `n` numbers, else nullptr.
static const nlohmann::json* GDF_Arr(const nlohmann::json& x, const char* key, size_t n) {
    auto it = x.find(key);
    if (it == x.end() || !it->is_array() || it->size() != n) {
        return nullptr;
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return nullptr;
        }
    }
    return &*it;
}

static uint8_t GDF_GetPhase(Actor* actor) {
    if (!GDF_IsDorf(actor)) {
        return GDF_PHASE_FIGHT;
    }
    return BossGanon_CoopPhase(actor);
}

static bool GDF_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    if (GDF_IsBall(actor)) {
        return true;
    }
    if (!GDF_IsDorf(actor)) {
        return false;
    }
    bool mirror = streamedPhase == GDF_PHASE_FIGHT && BossGanon_CoopPhase(actor) == GDF_PHASE_FIGHT &&
                  actor->colChkInfo.health > 0;
    sMirror.mirroring = mirror;
    return mirror;
}

static bool GDF_HandlesDefeat(Actor* actor) {
    return GDF_IsDorf(actor);
}

static void GDF_Serialize(Actor* actor, nlohmann::json& x) {
    if (actor->id != ACTOR_BOSS_GANON) {
        return;
    }
    BossGanon* b = (BossGanon*)actor;
    if (GDF_IsBall(actor)) {
        x["sc"] = actor->scale.x;
        x["f1"] = b->fwork[GDF_FWORK_1];
        x["f0"] = b->fwork[GDF_FWORK_0];
        x["a8"] = b->unk_1A8;
        x["a2"] = b->unk_1A2;
        x["rz"] = actor->shape.rot.z;
        x["ry"] = actor->shape.rot.y;
        return;
    }
    if (!GDF_IsDorf(actor)) {
        return;
    }
    x["ac"] = BossGanon_CoopAction(actor);
    x["lg"] = { b->legRot.x, b->legRot.y, b->legRot.z };
    x["oh"] = b->useOpenHand ? 1 : 0;
    x["hb"] = { b->handLightBallScale, b->unk_258 };
    x["bm"] = { b->unk_284, b->unk_288, b->unk_28C, b->unk_290, b->unk_2D0, (float)b->unk_1AC, (float)b->unk_1AA,
                b->unk_278.x, b->unk_278.y, b->unk_278.z };
    nlohmann::json rays = nlohmann::json::array();
    for (int i = 0; i < 15; i++) {
        rays.push_back(b->unk_294[i]);
    }
    x["ra"] = rays;
    x["sh"] = { b->unk_2E6, b->unk_2E8, b->unk_2D4, b->unk_1A6, b->shockGlow ? 1 : 0, b->unk_508 };
    if (b->unk_2E6 != 0 || b->unk_2E8 != 0) {
        nlohmann::json sa = nlohmann::json::array();
        for (int i = 0; i < 18; i++) {
            sa.push_back(b->unk_49C[i]);
        }
        x["sa"] = sa;
    }
    x["tf"] = { b->fwork[GDF_TRIFORCE_PRIM_B], b->fwork[GDF_TRIFORCE_PRIM_A], b->fwork[GDF_TRIFORCE_ENV_G],
                b->fwork[GDF_TRIFORCE_SCALE], b->fwork[GDF_VORTEX_ALPHA],     b->fwork[GDF_VORTEX_SCALE] };
    x["el"] = b->envLightMode;
    x["lf"] = { b->lensFlareMode, b->lensFlareTimer, b->lensFlareScale };
    x["wf"] = { b->whiteFillAlpha, b->screenFlashTimer, b->windowShatterState };
    x["fl"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) ? 1 : 0;

    nlohmann::json ev = nlohmann::json::array();
    for (int i = 0; i < GDF_RING; i++) {
        const GdfEvent& e = sMirror.ring[i];
        if (e.seq != 0) {
            ev.push_back({ e.seq, e.kind, e.params, e.pos.x, e.pos.y, e.pos.z, e.rot.x, e.rot.y, e.rot.z });
        }
    }
    x["ev"] = ev;
}

static void GDF_Replay(Actor* boss, const GdfEvent& e) {
    sMirror.replaying = true;
    if (e.kind == GDF_PLATFORM_KIND) {
        Vec3f pos = e.pos;
        BossGanon_CheckFallingPlatforms((BossGanon*)boss, gPlayState, &pos);
    } else {
        Actor* prevChild = boss->child;
        Actor_SpawnAsChild(&gPlayState->actorCtx, boss, gPlayState, e.kind, e.pos.x, e.pos.y, e.pos.z, e.rot.x,
                           e.rot.y, e.rot.z, e.params);
        boss->child = prevChild;
    }
    sMirror.replaying = false;
}

// A tennis ball on a mirror machine that touches that machine's Link hurts it the way the
// authority's ball hurts its own, once, and tells the authority to end the ball.
static void GDF_BallTouchesLocalLink(Actor* ball) {
    Player* player = GET_PLAYER(gPlayState);
    uint32_t frame = gPlayState->gameplayFrames;
    if (frame - sMirror.ballHitFrame < 20) {
        return;
    }
    float dx = player->actor.world.pos.x - ball->world.pos.x;
    float dy = player->actor.world.pos.y + 40.0f - ball->world.pos.y;
    float dz = player->actor.world.pos.z - ball->world.pos.z;
    if (sqrtf(dx * dx + dy * dy + dz * dz) > 25.0f) {
        return;
    }
    sMirror.ballHitFrame = frame;
    func_8002F6D4(gPlayState, ball, 3.0f, ball->world.rot.y, 0.0f, 0x30);
    SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &ball->world.pos, 40, NA_SE_EN_GANON_HIT_THUNDER);
    EnemySync::SendAdapterEvent(ball, GDF_EVENT_BALL_HIT_LINK);
}

static void GDF_Deserialize(Actor* actor, const nlohmann::json& x) {
    if (actor->id != ACTOR_BOSS_GANON || gPlayState == NULL) {
        return;
    }
    BossGanon* b = (BossGanon*)actor;
    if (GDF_IsBall(actor)) {
        actor->scale.x = actor->scale.y = actor->scale.z = GDF_F(x, "sc", actor->scale.x);
        b->fwork[GDF_FWORK_1] = GDF_F(x, "f1", b->fwork[GDF_FWORK_1]);
        b->fwork[GDF_FWORK_0] = GDF_F(x, "f0", b->fwork[GDF_FWORK_0]);
        b->unk_1A8 = (s16)GDF_I(x, "a8", b->unk_1A8);
        b->unk_1A2 = (s16)GDF_I(x, "a2", b->unk_1A2);
        actor->shape.rot.z = (s16)GDF_I(x, "rz", actor->shape.rot.z);
        actor->shape.rot.y = (s16)GDF_I(x, "ry", actor->shape.rot.y);
        if (b->unk_1A8 == 0) {
            GDF_BallTouchesLocalLink(actor);
        }
        return;
    }
    if (!GDF_IsDorf(actor)) {
        return;
    }

    uint32_t frame = gPlayState->gameplayFrames;
    bool fresh = sMirror.boss != actor || frame - sMirror.frame > 2;
    sMirror.boss = actor;
    sMirror.frame = frame;

    if (auto a = GDF_Arr(x, "lg", 3)) {
        b->legRot.x = (*a)[0].get<float>();
        b->legRot.y = (*a)[1].get<float>();
        b->legRot.z = (*a)[2].get<float>();
    }
    b->useOpenHand = GDF_I(x, "oh") != 0;
    if (auto a = GDF_Arr(x, "hb", 2)) {
        b->handLightBallScale = (*a)[0].get<float>();
        b->unk_258 = (*a)[1].get<float>();
    }
    if (auto a = GDF_Arr(x, "bm", 10)) {
        b->unk_284 = (*a)[0].get<float>();
        b->unk_288 = (*a)[1].get<float>();
        b->unk_28C = (*a)[2].get<float>();
        b->unk_290 = (*a)[3].get<float>();
        b->unk_2D0 = (*a)[4].get<float>();
        b->unk_1AC = (s16)(*a)[5].get<float>();
        b->unk_1AA = (s16)(*a)[6].get<float>();
        b->unk_278.x = (*a)[7].get<float>();
        b->unk_278.y = (*a)[8].get<float>();
        b->unk_278.z = (*a)[9].get<float>();
    }
    if (auto a = GDF_Arr(x, "ra", 15)) {
        for (int i = 0; i < 15; i++) {
            b->unk_294[i] = (*a)[i].get<float>();
        }
    }
    if (auto a = GDF_Arr(x, "sh", 6)) {
        b->unk_2E6 = (s16)(*a)[0].get<float>();
        b->unk_2E8 = (s16)(*a)[1].get<float>();
        b->unk_2D4 = (s16)(*a)[2].get<float>();
        b->unk_1A6 = (s16)(*a)[3].get<float>();
        b->shockGlow = (*a)[4].get<float>() != 0.0f;
        b->unk_508 = (*a)[5].get<float>();
    }
    if (auto a = GDF_Arr(x, "sa", 18)) {
        for (int i = 0; i < 18; i++) {
            b->unk_49C[i] = (*a)[i].get<float>();
        }
    }
    if (auto a = GDF_Arr(x, "tf", 6)) {
        b->fwork[GDF_TRIFORCE_PRIM_B] = (*a)[0].get<float>();
        b->fwork[GDF_TRIFORCE_PRIM_A] = (*a)[1].get<float>();
        b->fwork[GDF_TRIFORCE_ENV_G] = (*a)[2].get<float>();
        b->fwork[GDF_TRIFORCE_SCALE] = (*a)[3].get<float>();
        b->fwork[GDF_VORTEX_ALPHA] = (*a)[4].get<float>();
        b->fwork[GDF_VORTEX_SCALE] = (*a)[5].get<float>();
    }
    b->envLightMode = (s8)GDF_I(x, "el");
    if (auto a = GDF_Arr(x, "lf", 3)) {
        b->lensFlareMode = (u8)(*a)[0].get<float>();
        b->lensFlareTimer = (s16)(*a)[1].get<float>();
        b->lensFlareScale = (*a)[2].get<float>();
    }
    if (auto a = GDF_Arr(x, "wf", 3)) {
        b->whiteFillAlpha = (*a)[0].get<float>();
        b->screenFlashTimer = (s16)(*a)[1].get<float>();
        b->windowShatterState = (u8)(*a)[2].get<float>();
    }
    if (GDF_I(x, "fl") != 0) {
        actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    }

    auto evIt = x.find("ev");
    if (evIt != x.end() && evIt->is_array()) {
        uint32_t maxSeq = 0;
        for (const auto& e : *evIt) {
            if (e.is_array() && e.size() == 9 && e[0].is_number()) {
                maxSeq = std::max(maxSeq, e[0].get<uint32_t>());
            }
        }
        if (fresh || maxSeq < sMirror.lastSeq) {
            // Skip the backlog (and resync if the authority changed).
            sMirror.lastSeq = maxSeq;
        } else {
            for (const auto& e : *evIt) {
                if (!e.is_array() || e.size() != 9 || e[0].get<uint32_t>() <= sMirror.lastSeq) {
                    continue;
                }
                bool numbers = true;
                for (const auto& v : e) {
                    numbers = numbers && v.is_number();
                }
                if (!numbers) {
                    continue;
                }
                GdfEvent ev;
                ev.seq = e[0].get<uint32_t>();
                ev.kind = e[1].get<int16_t>();
                ev.params = e[2].get<uint16_t>();
                ev.pos = { e[3].get<float>(), e[4].get<float>(), e[5].get<float>() };
                ev.rot = { e[6].get<int16_t>(), e[7].get<int16_t>(), e[8].get<int16_t>() };
                GDF_Replay(actor, ev);
            }
            sMirror.lastSeq = maxSeq;
        }
    }
    BossGanon_CoopMirrorUpdate(actor, gPlayState);
}

// The body collider follows the chest (set in the draw). While he is stunned, hurt or
// recovering he does not hurt on touch, and while he blocks he is metal.
static void GDF_PositionCollider(Actor* actor, Collider* col) {
    if (!GDF_IsDorf(actor)) {
        return;
    }
    BossGanon* b = (BossGanon*)actor;
    BossGanon_CoopSetColliderPos(actor);
    uint8_t act = BossGanon_CoopAction(actor);
    col->colType = act == GDF_ACT_BLOCK ? 9 : 3;
    if (act == GDF_ACT_HIT_BY_BALL || act == GDF_ACT_VULNERABLE || act == GDF_ACT_DAMAGED) {
        b->collider.base.atFlags &= ~AT_ON;
    } else {
        b->collider.base.atFlags |= AT_ON;
    }
}

static bool GDF_LocalIntroRunning(Actor* actor) {
    return BossGanon_CoopPhase(actor) == GDF_PHASE_INTRO;
}

static bool GDF_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[GanondorfSync] phase {}->{}", fromPhase, toPhase);
    if (!GDF_IsDorf(actor) || toPhase != GDF_PHASE_DEFEATED || gPlayState == NULL) {
        return false;
    }
    if (GDF_LocalIntroRunning(actor)) {
        sMirror.pendingDefeat = true;
        sMirror.pendingBoss = actor;
        ESYNC_LOG("[GanondorfSync] defeat deferred until the local intro ends");
        return true;
    }
    BossGanon_StartDefeat(actor, gPlayState);
    ESYNC_LOG("[GanondorfSync] defeat handoff (death started locally)");
    return true;
}

static void GDF_OnRemoteDefeat(Actor* actor) {
    if (!GDF_IsDorf(actor) || gPlayState == NULL || BossGanon_CoopPhase(actor) == GDF_PHASE_DEFEATED) {
        return;
    }
    if (GDF_LocalIntroRunning(actor)) {
        sMirror.pendingDefeat = true;
        sMirror.pendingBoss = actor;
        ESYNC_LOG("[GanondorfSync] remote defeat deferred until the local intro ends");
        return;
    }
    BossGanon_StartDefeat(actor, gPlayState);
    ESYNC_LOG("[GanondorfSync] remote defeat (missed phase edge, death started locally)");
}

// Mirroring ended without a defeat: the AI takes over from the streamed pose, back in the
// hover that every attack returns to.
static void GDF_OnLocalResume(Actor* actor) {
    ESYNC_LOG("[GanondorfSync] local AI resumes");
    sMirror.mirroring = false;
    if (!GDF_IsDorf(actor) || gPlayState == NULL || BossGanon_CoopPhase(actor) != GDF_PHASE_FIGHT) {
        return;
    }
    BossGanon_CoopResume(actor, gPlayState);
}

static void GDF_OnRemoteEvent(Actor* actor, uint8_t event) {
    if (gPlayState == NULL) {
        return;
    }
    if (event == GDF_EVENT_BALL_HIT_LINK && GDF_IsBall(actor)) {
        BossGanon_CoopBallHitPeer(actor, gPlayState);
    } else if (event == GDF_EVENT_BALL_REACHED_DORF && GDF_IsDorf(actor) &&
               BossGanon_CoopPhase(actor) == GDF_PHASE_FIGHT && actor->colChkInfo.health > 0) {
        BossGanon_CoopHitByBall(actor, gPlayState);
    }
}

void RegisterGanondorfAdapter() {
    ActorSyncAdapter a;
    a.SerializeExtras = GDF_Serialize;
    a.DeserializeExtras = GDF_Deserialize;
    a.GetPhase = GDF_GetPhase;
    a.OnPhaseChange = GDF_OnPhaseChange;
    a.ShouldMirror = GDF_ShouldMirror;
    a.OnRemoteDefeat = GDF_OnRemoteDefeat;
    a.OnLocalResume = GDF_OnLocalResume;
    a.OnRemoteEvent = GDF_OnRemoteEvent;
    a.HandlesDefeat = GDF_HandlesDefeat;
    a.DropUnconsumedHits = true;
    a.PositionCollider = GDF_PositionCollider;
    EnemySync::RegisterAdapter(ACTOR_BOSS_GANON, a);
}

// z_boss_ganon.c, when his intro cutscene ends: the partner may have won while it played.
extern "C" void Anchor_GanondorfIntroOver(Actor* dorf) {
    if (!sMirror.pendingDefeat || sMirror.pendingBoss != dorf || gPlayState == NULL) {
        return;
    }
    sMirror.pendingDefeat = false;
    sMirror.pendingBoss = nullptr;
    BossGanon_StartDefeat(dorf, gPlayState);
    ESYNC_LOG("[GanondorfSync] local intro over: joining the deferred defeat");
}

// z_boss_ganon.c, Init: the authority notes the effect spawns a mirror should replay (see the
// file comment). Spawns made by a replay are never recorded again.
extern "C" void Anchor_GanondorfSpawned(Actor* spawned) {
    if (sMirror.replaying || !EnemySync::IsLocalAuthority() || !EnemySync::HasSameScenePeer() ||
        spawned->params < (int16_t)GDF_PARAM_EFFECT_MIN) {
        return;
    }
    GdfEvent& e = sMirror.ring[sMirror.nextSeq % GDF_RING];
    e.seq = ++sMirror.nextSeq;
    e.kind = spawned->id;
    e.params = (uint16_t)spawned->params;
    e.pos = spawned->home.pos;
    e.rot = spawned->home.rot;
}

// z_boss_ganon.c, BossGanon_CheckFallingPlatforms: a platform fell on the authority.
extern "C" void Anchor_GanondorfPlatformCheck(Vec3f* pos) {
    if (sMirror.replaying || !EnemySync::IsLocalAuthority() || !EnemySync::HasSameScenePeer()) {
        return;
    }
    GdfEvent& e = sMirror.ring[sMirror.nextSeq % GDF_RING];
    e.seq = ++sMirror.nextSeq;
    e.kind = GDF_PLATFORM_KIND;
    e.params = 0;
    e.pos = *pos;
    e.rot = { 0, 0, 0 };
}

// z_boss_ganon.c, the big-magic ball: a reflected ball reached Ganondorf. Only the
// authority's own Ganondorf can take the hit; a mirror reports it.
extern "C" void Anchor_GanondorfBallReachedDorf(Actor* dorf) {
    if (gPlayState == NULL) {
        return;
    }
    if (EnemySync::IsSuppressed(dorf)) {
        EnemySync::SendAdapterEvent(dorf, GDF_EVENT_BALL_REACHED_DORF);
    } else if (BossGanon_CoopPhase(dorf) == GDF_PHASE_FIGHT) {
        BossGanon_CoopHitByBall(dorf, gPlayState);
    }
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4054 boss rig. Reports Ganondorf's sync state on this client:
// cmd 0 report only; 1 land a sword hit on the body collider the way the collision check
// would (arg = dmgFlags, default sword); 2 set the boss health to arg (authority);
// 3 un-clear this room; 4 teleport Link to (arg, 100, 0); 5 hit the tennis ball with a sword;
// 6 start the death cutscene (authority); 7 a reflected ball reaches him (authority).
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_gdf(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossGanon* boss = nullptr;
    BossGanon* ball = nullptr;
    int effects = 0, bigBalls = 0, hearts = 0, warps = 0, platformsFalling = 0;
    nlohmann::json all = nlohmann::json::array();
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->update == NULL) {
                continue;
            }
            if (a->id == ACTOR_BOSS_GANON) {
                all.push_back({ (uint16_t)a->params, a->category });
                if (GDF_IsDorf(a)) {
                    boss = (BossGanon*)a;
                } else if (GDF_IsBall(a)) {
                    ball = (BossGanon*)a;
                } else if (a->params >= 0x104 && a->params < 0x12C) {
                    bigBalls++;
                } else {
                    effects++;
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
    j["effects"] = effects;
    j["all"] = all;
    j["bigBalls"] = bigBalls;
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    (void)platformsFalling;
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
        j["ballMode"] = ball->unk_1C2;
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
    j["acPre"] = (boss->collider.base.acFlags & AC_HIT) != 0;
    if (cmd == 1) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = arg != 0 ? (u32)arg : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        boss->collider.base.acFlags |= AC_HIT;
        boss->collider.base.ac = &player->actor;
        boss->collider.info.bumperFlags |= BUMP_HIT;
        boss->collider.info.acHitInfo = &sToucher;
    } else if (cmd == 2 && !EnemySync::IsSuppressed(&boss->actor)) {
        boss->actor.colChkInfo.health = arg;
    } else if (cmd == 6 && !EnemySync::IsSuppressed(&boss->actor)) {
        BossGanon_StartDefeat(&boss->actor, gPlayState);
    } else if (cmd == 7 && !EnemySync::IsSuppressed(&boss->actor)) {
        // a reflected ball reaching him
        BossGanon_CoopHitByBall(&boss->actor, gPlayState);
    }
    j["phase"] = GDF_GetPhase(&boss->actor);
    j["hp"] = boss->actor.colChkInfo.health;
    j["act"] = BossGanon_CoopAction(&boss->actor);
    j["cs"] = boss->csState;
    j["sup"] = EnemySync::IsSuppressed(&boss->actor);
    j["dying"] = EnemySync::IsDying(&boss->actor);
    j["pos"] = { boss->actor.world.pos.x, boss->actor.world.pos.y, boss->actor.world.pos.z };
    j["pending"] = sMirror.pendingDefeat;
    j["ringSeq"] = sMirror.nextSeq;
    j["replaySeq"] = sMirror.lastSeq;
    j["unk1C2"] = boss->unk_1C2;
    j["t0"] = boss->timers[0];
    out = j.dump();
    return out.c_str();
}
}
#endif

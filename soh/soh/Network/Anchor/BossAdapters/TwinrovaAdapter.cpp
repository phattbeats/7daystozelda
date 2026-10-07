#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Tw/z_boss_tw.h"
extern PlayState* gPlayState;
}

/**
 * Twinrova (PHA-4053), the Gohma treatment. Three tracked actors share ACTOR_BOSS_TW, told apart by params:
 * Kotake (0), Koume (1) and Twinrova (2); the blasts they throw are params 0x64 and up. All three are spawned
 * by Twinrova's own Init on every client, so they keep their room-occurrence keys (OnEnemyActorSpawn skips
 * params <= 2). Only the blasts are dynamic spawns.
 *
 * Stages. The fight has five, held in one file-static (TwStage) that every actor reports as its phase:
 * INTRO (the intro cutscene), WITCHES (Kotake and Koume fly, shoot beams, and are hurt by reflected beams),
 * MERGE (the cutscene where they become Twinrova), TWINROVA (her blasts, the stun, the sword), DEFEATED. The
 * stage is set where the code changes it, never read off actionFunc. The three cutscenes (intro, merge,
 * death) own the camera and Link, so each client runs its own: a client mirrors an actor only while its own
 * stage equals the streamed one and the stage is a fighting one (witches in WITCHES, Twinrova and the blasts
 * in TWINROVA). A client that is still in an earlier cutscene when the host moves on starts the next one
 * itself (OnPhaseChange / ShouldMirror), then joins.
 *
 * Extras (what Update computes and Draw reads). Witches: action code, visibility and the stunned-hair flag,
 * the scepter, flame, portal and beam fields, the reflected beam's origin, pitch, yaw and reach, the pool
 * values that ride in workf, fog and flash timers, eyes. Twinrova: action code, eyes, the workf pool and
 * flash values, timers, the room-light type and ground-blast type, the stun flag. Blasts: scale, tail alpha,
 * state, type, timer. Mirrors keep local what is local: the effect array, the room light, the scepter and
 * crown sparks, the blast's tail, the scrolling textures (BossTw_AnchorMirrorTick). Nothing that spawns pieces
 * from Draw is streamed.
 *
 * Children (decision). Blasts (fire 0x64, ice 0x66) are tracked dynamic spawns: they are the projectiles the
 * players interact with. The pools they leave (0x65, 0x67) and the death balls (0x68, 0x69) are excluded
 * (IsTrackingExcluded) and are local effects: the host records each pool spawn in a small ring that rides in
 * the Kotake and Twinrova extras and every mirror replays it (the pool then runs its own damage check against
 * the local Link, and its fade follows the streamed workf values); the death balls come from each client's own
 * death cutscene.
 *
 * Aggro and the mirror shield (decision). Bosses stay out of the EnemyTargeting puppet swap (the three
 * cutscenes and the camera read GET_PLAYER). The attacks aim at the NEAREST living player on the host
 * (Anchor_BossNearestTarget): the beam's aim point and the blasts' launch direction. What happens when a
 * beam or blast reaches a player is decided on that player's own machine, because the mirror shield is
 * about that machine's Link (stateFlags1, shieldMf, the R button, the charge glow):
 *  - A beam is a ray in the room. Each machine tests its own Link against the streamed ray with the stock
 *    checks (BossTw_AnchorBeamVictim): a hit freezes or burns that Link locally; a mirror-shield catch is
 *    reported to the host (EV_BEAM_REFLECT) and then the reflector reports its shield pose every frame
 *    (EV_REFLECT_STATE), which the host's reflect state follows instead of GET_PLAYER (anchorReflector).
 *  - A blast that meets a mirror's shield is judged by the mirror (BossTw_AnchorBlastShield, via OnLocalHit):
 *    the charge is per player (each machine keeps its own sShieldFire/IceCharge and glow), and the result,
 *    absorbed or charged-and-released, is reported to the host (EV_BLAST_ABSORB). A release follows the
 *    reflector's reported pose for its 80 frames and fires the stunning shot from there.
 *  So who absorbs which beam is per player, and the outcome (beam clipped, witch hit, blast consumed, Twinrova
 *  stunned) is decided once, on the host, and streamed to everyone.
 *
 * Defeat: FIGHT -> DEFEATED runs Twinrova's own SetupDeathCS locally (plus the finishing blow and the defeat
 * hook) and releases her, so the death cutscene, heart container, blue warp and clear flag happen on every
 * client. A client still in a cutscene of its own waits for it to end.
 */

enum TwEvent : uint8_t {
    EV_BEAM_REFLECT = 1,
    EV_REFLECT_STATE = 2,
    EV_BEAM_HIT = 3,
    EV_BLAST_ABSORB = 4,
};

static constexpr int TW_RING = 8;

struct TwGroundEvent {
    uint32_t seq = 0;
    int type = 0;
    Vec3f pos = { 0, 0, 0 };
    int timer = 0;
};

struct TwState {
    TwGroundEvent ring[TW_RING];
    uint32_t nextSeq = 0;
    uint32_t replayed = 0;
    bool replayFresh = true;
    uint32_t replayFrame = 0;
    bool pendingDefeat = false;
    // Mirror side: the end of a charged release that left this machine's shield (frame, blast type).
    uint32_t releaseDoneFrame = 0;
    int releaseType = 0;
    uint32_t lastReflectSent = 0;
};
static TwState sTw;

static BossTw* TW_Boss(Actor* a) {
    return a != nullptr && a->id == ACTOR_BOSS_TW ? (BossTw*)a : nullptr;
}

static bool TW_IsBlast(Actor* a) {
    return (uint16_t)a->params >= TW_FIRE_BLAST;
}

static bool TW_IsTwinrova(Actor* a) {
    return (uint16_t)a->params == TW_TWINROVA;
}

static bool TW_IsBlastBody(Actor* a) {
    return (uint16_t)a->params == TW_FIRE_BLAST || (uint16_t)a->params == TW_ICE_BLAST;
}

static uint32_t TW_OwnId() {
    return Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
}

// ---------------------------------------------------------------------------
// JSON helpers: a wrong type throws a C++ exception that kills the page, so read every field by type.
// ---------------------------------------------------------------------------
static double TW_Num(const nlohmann::json& x, const char* key, double dflt = 0.0) {
    auto it = x.find(key);
    return (it != x.end() && it->is_number()) ? it->get<double>() : dflt;
}

static bool TW_Has(const nlohmann::json& x, const char* key) {
    auto it = x.find(key);
    return it != x.end() && it->is_number();
}

static bool TW_Arr(const nlohmann::json& x, const char* key, size_t n) {
    auto it = x.find(key);
    if (it == x.end() || !it->is_array() || it->size() != n) {
        return false;
    }
    for (const auto& v : *it) {
        if (!v.is_number()) {
            return false;
        }
    }
    return true;
}

static nlohmann::json TW_Vec(const Vec3f& v) {
    return nlohmann::json::array({ v.x, v.y, v.z });
}

static Vec3f TW_ToVec(const nlohmann::json& a) {
    return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>() };
}

// ---------------------------------------------------------------------------
// Phases
// ---------------------------------------------------------------------------
static uint8_t TW_GetPhase(Actor*) {
    return (uint8_t)BossTw_AnchorStage();
}

static bool TW_LocalCutsceneStage(int stage) {
    return stage == TW_STAGE_INTRO || stage == TW_STAGE_MERGE;
}

static bool TW_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    int stage = BossTw_AnchorStage();
    if (gPlayState == NULL) {
        return false;
    }
    if (TW_IsTwinrova(actor)) {
        // The host moved on while this client was in an earlier cutscene or joined late: start the next one
        // here (both are idempotent on the stage).
        if (streamedPhase >= TW_STAGE_MERGE && streamedPhase < TW_STAGE_DEFEATED && stage == TW_STAGE_WITCHES) {
            BossTw_AnchorStartMerge(gPlayState);
            return false;
        }
        if ((streamedPhase == TW_STAGE_DEFEATED || sTw.pendingDefeat) && !TW_LocalCutsceneStage(stage) &&
            stage != TW_STAGE_DEFEATED) {
            sTw.pendingDefeat = false;
            BossTw_AnchorStartDefeat((BossTw*)actor, gPlayState);
            ESYNC_LOG("[TwinrovaSync] deferred defeat started");
            return false;
        }
        return streamedPhase == TW_STAGE_TWINROVA && stage == TW_STAGE_TWINROVA;
    }
    if (TW_IsBlast(actor)) {
        return streamedPhase == TW_STAGE_TWINROVA && stage == TW_STAGE_TWINROVA;
    }
    return streamedPhase == TW_STAGE_WITCHES && stage == TW_STAGE_WITCHES;
}

static bool TW_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[TwinrovaSync] id-params={} phase {}->{} (local stage {})", (uint16_t)actor->params, fromPhase,
              toPhase, BossTw_AnchorStage());
    if (gPlayState == NULL) {
        return false;
    }
    int stage = BossTw_AnchorStage();
    if (toPhase >= TW_STAGE_MERGE && toPhase < TW_STAGE_DEFEATED && stage == TW_STAGE_WITCHES && TW_IsTwinrova(actor)) {
        BossTw_AnchorStartMerge(gPlayState);
        return false;
    }
    if (toPhase != TW_STAGE_DEFEATED) {
        return false;
    }
    if (!TW_IsTwinrova(actor)) {
        // The witches and the blasts follow Twinrova's own death cutscene; let their local code run on.
        return true;
    }
    if (TW_LocalCutsceneStage(stage)) {
        sTw.pendingDefeat = true;
        ESYNC_LOG("[TwinrovaSync] defeat deferred until the local cutscene ends");
        return false;
    }
    BossTw_AnchorStartDefeat((BossTw*)actor, gPlayState);
    ESYNC_LOG("[TwinrovaSync] defeat handoff (death cutscene started locally)");
    return true;
}

static void TW_OnRemoteDefeat(Actor* actor) {
    if (gPlayState == NULL || !TW_IsTwinrova(actor)) {
        return;
    }
    int stage = BossTw_AnchorStage();
    if (stage == TW_STAGE_DEFEATED) {
        return;
    }
    if (TW_LocalCutsceneStage(stage)) {
        sTw.pendingDefeat = true;
        return;
    }
    BossTw_AnchorStartDefeat((BossTw*)actor, gPlayState);
    ESYNC_LOG("[TwinrovaSync] remote defeat (missed phase edge, death cutscene started locally)");
}

static bool TW_HandlesDefeat(Actor* actor) {
    return TW_IsTwinrova(actor);
}

static void TW_OnLocalResume(Actor* actor) {
    BossTw* t = TW_Boss(actor);
    ESYNC_LOG("[TwinrovaSync] local AI resumes (params {})", (uint16_t)actor->params);
    if (t != nullptr && gPlayState != NULL) {
        t->anchorLocalReflect = 0;
        BossTw_AnchorResume(t, gPlayState);
    }
}

// the stage a client's own cutscene ends in (z_boss_tw.c): a defeat that arrived during it starts now
extern "C" void Anchor_TwStage(int stage) {
    if (sTw.pendingDefeat && (stage == TW_STAGE_WITCHES || stage == TW_STAGE_TWINROVA) && gPlayState != NULL) {
        BossTw* tw = BossTw_AnchorGlobal(2);
        if (tw != nullptr) {
            sTw.pendingDefeat = false;
            BossTw_AnchorStartDefeat(tw, gPlayState);
            ESYNC_LOG("[TwinrovaSync] local cutscene over: joining the deferred defeat");
        }
    }
}

// ---------------------------------------------------------------------------
// Ground-pool spawns: the host notes them, mirrors replay them once
// ---------------------------------------------------------------------------
extern "C" void Anchor_TwGroundBlastSpawned(int type, Vec3f* pos, int16_t timer) {
    if (!EnemySync::IsLocalAuthority() || !EnemySync::HasSameScenePeer()) {
        return;
    }
    TwGroundEvent& e = sTw.ring[sTw.nextSeq % TW_RING];
    e.seq = ++sTw.nextSeq;
    e.type = type;
    e.pos = *pos;
    e.timer = timer;
}

// The host took a pool back (a shield stopped the beam that made it): mirrors must not replay it.
extern "C" void Anchor_TwGroundBlastCancelled() {
    for (TwGroundEvent& e : sTw.ring) {
        if (e.seq != 0 && e.seq == sTw.nextSeq) {
            e.type = 0;
        }
    }
}

static void TW_PutRing(nlohmann::json& x) {
    nlohmann::json ev = nlohmann::json::array();
    for (int i = 0; i < TW_RING; i++) {
        const TwGroundEvent& e = sTw.ring[i];
        if (e.seq != 0) {
            ev.push_back({ e.seq, e.type, e.pos.x, e.pos.y, e.pos.z, e.timer });
        }
    }
    x["gb"] = ev;
}

static void TW_ReplayPool(const TwGroundEvent& e) {
    BossTw* tw = BossTw_AnchorGlobal(2);
    if (tw == nullptr || gPlayState == NULL) {
        return;
    }
    int16_t params = e.type == 1 ? TW_FIRE_BLAST_GROUND : TW_ICE_BLAST_GROUND;
    Actor* a = Actor_SpawnAsChild(&gPlayState->actorCtx, &tw->actor, gPlayState, ACTOR_BOSS_TW, e.pos.x, e.pos.y,
                                  e.pos.z, 0, 0, 0, params);
    if (a != nullptr) {
        ((BossTw*)a)->timers[0] = (s16)e.timer;
    }
}

static void TW_ReadRing(const nlohmann::json& x) {
    auto it = x.find("gb");
    if (it == x.end() || !it->is_array() || gPlayState == NULL) {
        return;
    }
    uint32_t frame = gPlayState->gameplayFrames;
    bool fresh = sTw.replayFresh || frame - sTw.replayFrame > 4;
    sTw.replayFrame = frame;
    sTw.replayFresh = false;
    uint32_t maxSeq = 0;
    for (const auto& e : *it) {
        if (e.is_array() && e.size() == 6 && e[0].is_number()) {
            maxSeq = std::max(maxSeq, e[0].get<uint32_t>());
        }
    }
    if (fresh || maxSeq < sTw.replayed) {
        sTw.replayed = maxSeq; // skip the backlog (and resync when the host changed)
        return;
    }
    for (const auto& e : *it) {
        bool ok = e.is_array() && e.size() == 6;
        for (size_t i = 0; ok && i < e.size(); i++) {
            ok = e[i].is_number();
        }
        if (!ok || e[0].get<uint32_t>() <= sTw.replayed) {
            continue;
        }
        TwGroundEvent ev;
        ev.seq = e[0].get<uint32_t>();
        ev.type = e[1].get<int>();
        if (ev.type == 0) {
            continue; // retracted
        }
        ev.pos = { e[2].get<float>(), e[3].get<float>(), e[4].get<float>() };
        ev.timer = e[5].get<int>();
        TW_ReplayPool(ev);
    }
    sTw.replayed = maxSeq;
}

// ---------------------------------------------------------------------------
// Extras
// ---------------------------------------------------------------------------
static void TW_PutFog(nlohmann::json& x, BossTw* t) {
    x["fc"] = nlohmann::json::array({ t->fogR, t->fogG, t->fogB, t->fogNear, t->fogFar });
}

static void TW_GetFog(const nlohmann::json& x, BossTw* t) {
    if (TW_Arr(x, "fc", 5)) {
        t->fogR = x["fc"][0].get<float>();
        t->fogG = x["fc"][1].get<float>();
        t->fogB = x["fc"][2].get<float>();
        t->fogNear = x["fc"][3].get<float>();
        t->fogFar = x["fc"][4].get<float>();
    }
}

// workf[9..19]: the pool, flame and flash values (the crown scroll in 0..7 runs locally)
static void TW_PutWork(nlohmann::json& x, BossTw* t, int lo, int hi) {
    nlohmann::json w = nlohmann::json::array();
    for (int i = lo; i <= hi; i++) {
        w.push_back(t->workf[i]);
    }
    x["w"] = w;
}

static void TW_GetWork(const nlohmann::json& x, BossTw* t, int lo, int hi) {
    if (TW_Arr(x, "w", (size_t)(hi - lo + 1))) {
        for (int i = lo; i <= hi; i++) {
            t->workf[i] = x["w"][i - lo].get<float>();
        }
    }
}

static void TW_Serialize(Actor* actor, nlohmann::json& x) {
    BossTw* t = TW_Boss(actor);
    if (t == nullptr) {
        return;
    }
    x["cs1"] = t->csState1;
    x["t0"] = t->timers[0];
    x["rr"] = t->anchorReflector;
    if (TW_IsBlast(actor)) {
        x["bt"] = t->blastType;
        x["ta"] = t->workf[TAIL_ALPHA];
        x["sc"] = actor->scale.x;
        x["ba"] = t->blastActive;
        return;
    }
    x["ac"] = BossTw_AnchorActionCode(t);
    x["vi"] = (int)t->visible | ((int)t->unk_5F8 << 1) | ((int)t->unk_5F9 << 2);
    x["ey"] = t->eyeTexIdx;
    x["ley"] = t->leftEyeTexIdx;
    x["ft"] = t->work[FOG_TIMER];
    TW_PutFog(x, t);
    x["env"] = BossTw_AnchorEnvType();
    x["gbt"] = BossTw_AnchorGroundBlastType();
    TW_PutRing(x);
    if (TW_IsTwinrova(actor)) {
        TW_PutWork(x, t, 9, 22);
        x["us8"] = t->work[UNK_S8];
        x["tm"] = nlohmann::json::array({ t->timers[0], t->timers[1], t->timers[2], t->timers[3], t->timers[4] });
        x["stun"] = t->twinrovaStun;
        x["tbt"] = BossTw_AnchorBlastType();
        return;
    }
    TW_PutWork(x, t, 9, 19);
    x["t1"] = t->timers[1];
    x["sa"] = t->scepterAlpha;
    x["fa"] = t->flameAlpha;
    x["pa"] = t->spawnPortalAlpha;
    x["ps"] = t->spawnPortalScale;
    x["fr"] = t->flameRotation;
    x["pr"] = t->portalRotation;
    x["bs"] = t->beamScale;
    x["bst"] = t->beamShootState;
    x["bd"] = t->beamDist;
    x["bp"] = t->beamPitch;
    x["by"] = t->beamYaw;
    x["br"] = t->beamRoll;
    x["rp"] = t->beamReflectionPitch;
    x["ry"] = t->beamReflectionYaw;
    x["rd"] = t->beamReflectionDist;
    x["ro"] = TW_Vec(t->beamReflectionOrigin);
    x["gp"] = TW_Vec(t->groundBlastPos2);
}

static void TW_SendEvent(Actor* actor, uint8_t ev, const nlohmann::json& data) {
    EnemySync::SendAdapterEvent(actor, ev, &data);
}

// This machine's shield, as the host's reflect state wants it: the body part the beam lands on and the
// direction the reflection leaves (the same formula BossTw_ReflectorDir uses for the host's own Link).
static void TW_ShieldPose(nlohmann::json& d) {
    Player* link = GET_PLAYER(gPlayState);
    Vec3s dir;
    Matrix_MtxFToYXZRotS(&link->shieldMf, &dir, 0);
    dir.y += 0x8000;
    dir.x = -dir.x;
    d["b"] = TW_Vec(link->bodyPartsPos[15]);
    d["dx"] = dir.x;
    d["dy"] = dir.y;
    d["h"] = CHECK_BTN_ALL(gPlayState->state.input[0].cur.button, BTN_R) ? 1 : 0;
}

static void TW_ApplyCommon(BossTw* t, const nlohmann::json& x) {
    if (TW_Has(x, "cs1")) t->csState1 = (s16)TW_Num(x, "cs1");
    if (TW_Has(x, "t0")) t->timers[0] = (s16)TW_Num(x, "t0");
    t->anchorReflector = (u32)TW_Num(x, "rr");
}

static void TW_Deserialize(Actor* actor, const nlohmann::json& x) {
    BossTw* t = TW_Boss(actor);
    if (t == nullptr || gPlayState == NULL) {
        return;
    }
    try {
        TW_ApplyCommon(t, x);
        if (TW_IsBlast(actor)) {
            if (TW_Has(x, "bt")) t->blastType = (s16)TW_Num(x, "bt");
            if (TW_Has(x, "ta")) t->workf[TAIL_ALPHA] = (float)TW_Num(x, "ta");
            if (TW_Has(x, "sc")) {
                float sc = (float)TW_Num(x, "sc");
                actor->scale.x = actor->scale.y = actor->scale.z = sc;
            }
            if (TW_Has(x, "ba")) t->blastActive = (u8)TW_Num(x, "ba");
            BossTw_AnchorMirrorTick(t, gPlayState);
            // A shield verdict sent from here is in flight: don't judge the same blast twice. It ends when the host's
            // state moves on, or after a second without an answer.
            if (t->anchorLocalReflect && t->csState1 == 1 && t->anchorReflectAge < 20) {
                t->anchorReflectAge++;
            } else if (t->anchorLocalReflect && t->csState1 != 10) {
                t->anchorLocalReflect = 0;
            }
            // The reflector's own screen: follow the charged release (this machine's shield) for the host.
            if (TW_IsBlastBody(actor) && t->csState1 == 10 && t->anchorReflector == TW_OwnId() && TW_OwnId() != 0) {
                nlohmann::json d;
                TW_ShieldPose(d);
                TW_SendEvent(actor, EV_REFLECT_STATE, d);
                BossTw_AnchorReflectSparks(t, gPlayState);
            }
            return;
        }

        if (TW_Has(x, "ac")) BossTw_AnchorSetAction(t, (u8)TW_Num(x, "ac"));
        if (TW_Has(x, "vi")) {
            int vi = (int)TW_Num(x, "vi");
            t->visible = vi & 1;
            t->unk_5F8 = (vi >> 1) & 1;
            t->unk_5F9 = (vi >> 2) & 1;
        }
        if (TW_Has(x, "ey")) t->eyeTexIdx = (s16)TW_Num(x, "ey");
        if (TW_Has(x, "ley")) t->leftEyeTexIdx = (s16)TW_Num(x, "ley");
        if (TW_Has(x, "ft")) t->work[FOG_TIMER] = (s16)TW_Num(x, "ft");
        TW_GetFog(x, t);
        BossTw_AnchorSetEnvType((s32)TW_Num(x, "env"), (s32)TW_Num(x, "gbt"));
        TW_ReadRing(x);

        if (TW_IsTwinrova(actor)) {
            TW_GetWork(x, t, 9, 22);
            if (TW_Has(x, "us8")) t->work[UNK_S8] = (s16)TW_Num(x, "us8");
            if (TW_Arr(x, "tm", 5)) {
                for (int i = 0; i < 5; i++) t->timers[i] = x["tm"][i].get<s16>();
            }
            if (TW_Has(x, "stun")) t->twinrovaStun = (u8)TW_Num(x, "stun");
            if (TW_Has(x, "tbt")) BossTw_AnchorSetBlastType((s32)TW_Num(x, "tbt"));
            BossTw_AnchorMirrorTick(t, gPlayState);
            if (sTw.releaseDoneFrame != 0 && gPlayState->gameplayFrames >= sTw.releaseDoneFrame) {
                sTw.releaseDoneFrame = 0;
                BossTw_AnchorReleaseDone(sTw.releaseType);
            }
            return;
        }

        TW_GetWork(x, t, 9, 19);
        if (TW_Has(x, "t1")) t->timers[1] = (s16)TW_Num(x, "t1");
        if (TW_Has(x, "sa")) t->scepterAlpha = (float)TW_Num(x, "sa");
        if (TW_Has(x, "fa")) t->flameAlpha = (float)TW_Num(x, "fa");
        if (TW_Has(x, "pa")) t->spawnPortalAlpha = (float)TW_Num(x, "pa");
        if (TW_Has(x, "ps")) t->spawnPortalScale = (float)TW_Num(x, "ps");
        if (TW_Has(x, "fr")) t->flameRotation = (float)TW_Num(x, "fr");
        if (TW_Has(x, "pr")) t->portalRotation = (float)TW_Num(x, "pr");
        if (TW_Has(x, "bs")) t->beamScale = (float)TW_Num(x, "bs");
        if (TW_Has(x, "bst")) t->beamShootState = (s16)TW_Num(x, "bst");
        if (TW_Has(x, "bd")) t->beamDist = (float)TW_Num(x, "bd");
        if (TW_Has(x, "bp")) t->beamPitch = (float)TW_Num(x, "bp");
        if (TW_Has(x, "by")) t->beamYaw = (float)TW_Num(x, "by");
        if (TW_Has(x, "br")) t->beamRoll = (float)TW_Num(x, "br");
        if (TW_Has(x, "rp")) t->beamReflectionPitch = (float)TW_Num(x, "rp");
        if (TW_Has(x, "ry")) t->beamReflectionYaw = (float)TW_Num(x, "ry");
        if (TW_Has(x, "rd")) t->beamReflectionDist = (float)TW_Num(x, "rd");
        if (TW_Arr(x, "ro", 3)) t->beamReflectionOrigin = TW_ToVec(x["ro"]);
        if (TW_Arr(x, "gp", 3)) t->groundBlastPos2 = TW_ToVec(x["gp"]);
        BossTw_AnchorMirrorTick(t, gPlayState);

        // This machine's Link against the beam the host is firing.
        uint32_t own = TW_OwnId();
        if (t->beamShootState == 1 && t->anchorReflector == own && own != 0) {
            nlohmann::json d;
            TW_ShieldPose(d);
            TW_SendEvent(actor, EV_REFLECT_STATE, d);
            t->anchorLocalReflect = 1;
        } else if (t->beamShootState == 0) {
            f32 dist = 0.0f;
            s32 r = BossTw_AnchorBeamVictim(t, gPlayState, &dist);
            if (r == 1) {
                nlohmann::json d;
                TW_ShieldPose(d);
                d["d"] = dist;
                TW_SendEvent(actor, EV_BEAM_REFLECT, d);
            } else if (r == 2) {
                TW_SendEvent(actor, EV_BEAM_HIT, nlohmann::json::object());
            }
            t->anchorLocalReflect = 0;
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[TwinrovaSync] extras parse error: {}", ex.what());
    }
}

// A blast met this machine's shield: the vanilla check judges it here (charge, glow, absorb or release),
// and the host is told the result.
static bool TW_OnLocalHit(Actor* actor) {
    BossTw* t = TW_Boss(actor);
    if (t == nullptr || !TW_IsBlastBody(actor) || gPlayState == NULL || t->csState1 != 1 ||
        !(t->collider.base.acFlags & AC_HIT)) {
        return false;
    }
    if (t->anchorLocalReflect) {
        t->collider.base.acFlags &= ~AC_HIT; // already judged, waiting for the host
        return true;
    }
    s32 r = BossTw_AnchorBlastShield(t, gPlayState);
    if (r == 0) {
        return false;
    }
    nlohmann::json d;
    TW_ShieldPose(d);
    d["r"] = r == 2 ? 1 : 0;
    if (r == 2) {
        d["dx"] = t->magicDir.x;
        d["dy"] = t->magicDir.y;
        sTw.releaseDoneFrame = gPlayState->gameplayFrames + 80;
        sTw.releaseType = t->blastType;
    }
    t->anchorLocalReflect = 1;
    t->anchorReflectAge = 0;
    TW_SendEvent(actor, EV_BLAST_ABSORB, d);
    ESYNC_LOG("[TwinrovaSync] blast shield hit judged here: {}", r == 2 ? "charged release" : "absorbed");
    return true;
}

static void TW_OnRemoteEventData(Actor* actor, uint8_t event, const nlohmann::json& d, uint32_t from) {
    BossTw* t = TW_Boss(actor);
    if (t == nullptr || !d.is_object()) {
        return;
    }
    if (event == EV_BEAM_HIT) {
        BossTw* twinrova = BossTw_AnchorGlobal(2);
        if (twinrova != nullptr) twinrova->timers[2] = 150; // the witches laugh
        return;
    }
    if (!TW_Arr(d, "b", 3) || !TW_Has(d, "dx") || !TW_Has(d, "dy")) {
        return;
    }
    Vec3f body = TW_ToVec(d["b"]);
    s16 dx = (s16)TW_Num(d, "dx");
    s16 dy = (s16)TW_Num(d, "dy");
    switch (event) {
        case EV_BEAM_REFLECT:
            BossTw_AnchorBeamReflected(t, (float)TW_Num(d, "d"), &body, dx, dy, from);
            ESYNC_LOG("[TwinrovaSync] beam reflected by client {}", from);
            break;
        case EV_REFLECT_STATE:
            BossTw_AnchorReflectReport(t, (s32)TW_Num(d, "h"), &body, dx, dy, from);
            break;
        case EV_BLAST_ABSORB:
            BossTw_AnchorBlastAbsorbed(t, (s32)TW_Num(d, "r"), &body, dx, dy, from);
            ESYNC_LOG("[TwinrovaSync] blast {} by client {}", TW_Num(d, "r") != 0 ? "released" : "absorbed", from);
            break;
        default:
            break;
    }
}

void RegisterTwinrovaAdapter() {
    ActorSyncAdapter a;
    a.SerializeExtras = TW_Serialize;
    a.DeserializeExtras = TW_Deserialize;
    a.GetPhase = TW_GetPhase;
    a.OnPhaseChange = TW_OnPhaseChange;
    a.ShouldMirror = TW_ShouldMirror;
    a.OnRemoteDefeat = TW_OnRemoteDefeat;
    a.OnLocalResume = TW_OnLocalResume;
    a.HandlesDefeat = TW_HandlesDefeat;
    a.OnLocalHit = TW_OnLocalHit;
    a.OnRemoteEventData = TW_OnRemoteEventData;
    EnemySync::RegisterAdapter(ACTOR_BOSS_TW, a);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// PHA-4053 boss rig. Reports the Twinrova fight on this client and pokes it:
// cmd 0 report only; 1 set the two witches' health to arg (authority; starts the merge once both fly);
// 2 set Twinrova's health to arg (authority); 3 clear the "began battle" flag and un-clear the room;
// 4 teleport Link to (arg, 240, 0); 5 set this machine's shield charge to arg (fire if blast type 1, else ice);
// 6 make Link adult with the Master Sword and Mirror Shield (applies on the next scene load); 7 force Twinrova to stun now (authority);
// 9 start a beam on witch `arg` (0 Kotake, 1 Koume) now (authority); 11 start a blast of type `arg` (1 fire,
// 0 ice) from Twinrova now (authority).
// Pin witch `which` (0 Kotake, 1 Koume, 2 Twinrova) at a point (authority only).
EMSCRIPTEN_KEEPALIVE
void anchor_test_tw_pos(int which, double x, double y, double z) {
    BossTw* t = which >= 0 && which < 3 ? BossTw_AnchorGlobal(which) : nullptr;
    if (t == nullptr || EnemySync::IsSuppressed(&t->actor)) {
        return;
    }
    t->actor.world.pos = { (f32)x, (f32)y, (f32)z };
    t->actor.prevPos = t->actor.world.pos;
}

EMSCRIPTEN_KEEPALIVE
const char* anchor_test_tw(int cmd, int arg) {
    static std::string out;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    Player* link = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["room"] = gPlayState->roomCtx.curRoom.num;
    j["roomStatus"] = gPlayState->roomCtx.status;
    j["age"] = gSaveContext.linkAge;
    j["layer"] = gSaveContext.sceneSetupIndex;
    j["numActors"] = gPlayState->numSetupActors;
    j["bossCat"] = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].length;
    j["enemyCat"] = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].length;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = TW_OwnId();
    j["stage"] = BossTw_AnchorStage();
    j["link"] = { link->actor.world.pos.x, link->actor.world.pos.y, link->actor.world.pos.z };
    j["health"] = gSaveContext.health;
    j["csAction"] = link->csAction;
    j["shielding"] = (link->stateFlags1 & PLAYER_STATE1_SHIELDING) != 0;
    j["mirror"] = Player_HasMirrorShieldEquipped(gPlayState);
    j["frozen"] = link->actor.freezeTimer;
    j["burning"] = link->bodyIsBurning;
    j["clear"] = Flags_GetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    j["began"] = Flags_GetEventChkInf(EVENTCHKINF_BEGAN_TWINROVA_BATTLE);
    int blasts = 0, pools = 0, balls = 0, hearts = 0, warps = 0, tws = 0;
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->update == NULL) continue;
            if (a->id == ACTOR_BOSS_TW) {
                tws++;
                uint16_t p = (uint16_t)a->params;
                if (p == TW_FIRE_BLAST || p == TW_ICE_BLAST) {
                    blasts++;
                    BossTw* bt = (BossTw*)a;
                    j["blast"] = { { "params", p }, { "cs1", bt->csState1 }, { "t0", bt->timers[0] },
                                   { "pos", { a->world.pos.x, a->world.pos.y, a->world.pos.z } },
                                   { "refl", bt->anchorReflector }, { "sup", EnemySync::IsSuppressed(a) },
                                   { "key", (unsigned long long)EnemySync::KeyForActor(a) } };
                }
                else if (p == TW_FIRE_BLAST_GROUND || p == TW_ICE_BLAST_GROUND) pools++;
                else if (p >= TW_DEATHBALL_KOTAKE) balls++;
            }
            hearts += a->id == ACTOR_ITEM_B_HEART;
            warps += a->id == ACTOR_DOOR_WARP1;
        }
    }
    if (cmd == 20) {
        nlohmann::json al = nlohmann::json::array();
        for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
            for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
                al.push_back({ a->id, cat, (uint16_t)a->params, a->room, a->update != NULL });
            }
        }
        j["actors"] = al;
    }
    j["twActors"] = tws;
    j["blasts"] = blasts;
    j["pools"] = pools;
    j["balls"] = balls;
    j["hearts"] = hearts;
    j["warps"] = warps;
    j["shieldCharge"] = BossTw_AnchorShieldCharge();
    BossTw* parts[3] = { BossTw_AnchorGlobal(0), BossTw_AnchorGlobal(1), BossTw_AnchorGlobal(2) };
    if (cmd == 3) {
        gSaveContext.eventChkInf[EVENTCHKINF_BEGAN_TWINROVA_BATTLE >> 4] &=
            ~(1 << (EVENTCHKINF_BEGAN_TWINROVA_BATTLE & 0xF));
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    } else if (cmd == 4) {
        link->actor.world.pos.x = (float)arg;
        link->actor.world.pos.y = 240.0f;
        link->actor.world.pos.z = 0.0f;
    } else if (cmd == 5) {
        BossTw_AnchorSetShieldCharge(arg);
    } else if (cmd == 6) {
        // adult Link with the Master Sword and the Mirror Shield; takes effect on the next scene load
        gSaveContext.linkAge = LINK_AGE_ADULT;
        gPlayState->linkAgeOnLoad = LINK_AGE_ADULT;
        gSaveContext.inventory.equipment |= (1 << 1) | (1 << 6);
        gSaveContext.equips.equipment = (gSaveContext.equips.equipment & ~0x00FF) | 0x32;
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
    }
    if (cmd == 9 && arg >= 0 && arg < 3 && parts[arg] != nullptr && !EnemySync::IsSuppressed(&parts[arg]->actor)) {
        BossTw_AnchorForceAttack(parts[arg], gPlayState, 0);
    } else if (cmd == 11 && parts[2] != nullptr && !EnemySync::IsSuppressed(&parts[2]->actor)) {
        BossTw_AnchorForceAttack(parts[2], gPlayState, arg);
    }
    static const char* names[3] = { "kotake", "koume", "twinrova" };
    for (int i = 0; i < 3; i++) {
        BossTw* t = parts[i];
        if (t == nullptr) continue;
        nlohmann::json p;
        if (cmd == 1 && i < 2 && !EnemySync::IsSuppressed(&t->actor)) t->actor.colChkInfo.health = arg;
        if (cmd == 2 && i == 2 && !EnemySync::IsSuppressed(&t->actor)) t->actor.colChkInfo.health = arg;
        if (cmd == 7 && i == 2 && !EnemySync::IsSuppressed(&t->actor)) t->twinrovaStun = 1;
        p["hp"] = t->actor.colChkInfo.health;
        p["act"] = BossTw_AnchorActionCode(t);
        p["pos"] = { t->actor.world.pos.x, t->actor.world.pos.y, t->actor.world.pos.z };
        p["sup"] = EnemySync::IsSuppressed(&t->actor);
        p["dying"] = EnemySync::IsDying(&t->actor);
        p["vis"] = t->visible;
        p["cs1"] = t->csState1;
        p["cs2"] = t->csState2;
        p["bst"] = t->beamShootState;
        p["bd"] = t->beamDist;
        p["refl"] = t->anchorReflector;
        p["rYaw"] = t->beamReflectionYaw;
        p["rPitch"] = t->beamReflectionPitch;
        p["rDist"] = t->beamReflectionDist;
        p["bYaw"] = t->beamYaw;
        p["rot"] = t->actor.shape.rot.y;
        p["tmr"] = { t->timers[0], t->timers[1], t->timers[2] };
        p["key"] = (unsigned long long)EnemySync::KeyForActor(&t->actor);
        j[names[i]] = p;
    }
    {
        Vec3s sd;
        Matrix_MtxFToYXZRotS(&link->shieldMf, &sd, 0);
        j["linkYaw"] = link->actor.shape.rot.y;
        j["shieldDir"] = { sd.x, sd.y, sd.z };
        j["bodyPart"] = { link->bodyPartsPos[15].x, link->bodyPartsPos[15].y, link->bodyPartsPos[15].z };
    }
    j["ringSeq"] = sTw.nextSeq;
    j["replaySeq"] = sTw.replayed;
    j["pending"] = sTw.pendingDefeat;
    out = j.dump();
    return out.c_str();
}
}
#endif

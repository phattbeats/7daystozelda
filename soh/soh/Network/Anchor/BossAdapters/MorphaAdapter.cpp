#include "soh/Network/Anchor/BossAdapters/ActorSyncAdapter.h"
#include "soh/Network/Anchor/EnemySync.h"
#include "soh/Network/Anchor/Anchor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Boss_Mo/z_boss_mo.h"
extern PlayState* gPlayState;
}

/**
 * Morpha (#4051), the Gohma treatment. The core and both tentacles are all ACTOR_BOSS_MO, so every hook
 * tells them apart by params (tentacles are >= BOSSMO_TENTACLE; the core is 0 after its Init).
 *
 * Phases come from the core's csState: the intro states are PREFIGHT, MO_BATTLE is FIGHT, MO_DEATH_START and
 * up is DEFEATED. A tentacle follows the core's phase. The intro runs on every client (it starts when that
 * client's Link steps into the room); mirroring starts once the local intro is over and the host is fighting.
 *
 * Extras. Core: health, hit count, action state, the base water level, fade alpha, draw flag, damage flash,
 * body scale. Tentacle: action state, the three per-joint arrays that Update fills and Draw reads (rotation,
 * stretch, width), the cut/melt state, alpha, pulse and ripple size, the water-level wobble, who it holds. The
 * mirror runs the cosmetic half of Update itself every frame (BossMo_TentVisualTick, BossMo_CoreVisualTick:
 * ripples, base bubbles, water texture scroll, the room's water level, the splash effects), so none of that
 * is streamed. Nothing the boss spawns from draw is streamed.
 *
 * Child actors: tentacle 1 is spawned by the core's Init (static key); tentacle 2 appears after the third
 * core hit and is a tracked dynamic spawn, adopted on the mirror via BossMo_AnchorSetTent2. The splashes,
 * droplets and bubbles are entries in a static effect array, not actors. The heart container and the blue
 * warp come from the death sequence, which runs locally on every client.
 *
 * Aggro: the tentacles and the core go after the NEAREST living player, not just the host. The grab, the
 * shake camera, the A/B mash and the pin are all about the local Link (GET_PLAYER, player->actor.parent), so
 * they are done by the victim's own machine: the host curls the tentacle around the remote player by
 * distance, streams who it holds, and the victim's machine grabs, pins, shakes, damages and releases its own
 * Link (MO_VictimDriver). Mashing off reports MO_EVENT_ESCAPE; the host lets go at the next swing.
 *
 * Defeat: FIGHT -> DEFEATED calls BossMo_StartDeath (the vanilla core-death branch) locally. A mirror still in
 * its own intro waits for the intro to end (Anchor_MorphaDefeatPending).
 */

enum MorphaPhase : uint8_t {
    MO_PHASE_PREFIGHT = 0,
    MO_PHASE_FIGHT = 1,
    MO_PHASE_DEFEATED = 2,
};

enum MorphaEvent : uint8_t {
    MO_EVENT_ESCAPE = 1,
};

struct MorphaVictim {
    Actor* tent = nullptr;
    bool shakeStarted = false;
    bool escaped = false;
    int shakeTick = 0;
    int mash = 0;
    int16_t lastAct = 0;
};
static MorphaVictim sVictims[3];
static Actor* sPendingDefeat = nullptr;

static bool MO_IsTent(Actor* a) {
    return (s16)a->params >= BOSSMO_TENTACLE;
}

static MorphaVictim& MO_VictimFor(Actor* tent) {
    for (auto& v : sVictims) {
        if (v.tent == tent) {
            return v;
        }
    }
    for (auto& v : sVictims) {
        if (v.tent == nullptr) {
            v = MorphaVictim();
            v.tent = tent;
            return v;
        }
    }
    sVictims[0] = MorphaVictim();
    sVictims[0].tent = tent;
    return sVictims[0];
}

static BossMo* MO_Core() {
    return BossMo_AnchorGlobal(0);
}

static uint8_t MO_GetPhase(Actor* actor) {
    BossMo* core = MO_IsTent(actor) ? MO_Core() : (BossMo*)actor;
    if (core == nullptr) {
        return MO_PHASE_PREFIGHT;
    }
    if (core->csState >= MO_DEATH_START) {
        return MO_PHASE_DEFEATED;
    }
    return core->csState == MO_BATTLE ? MO_PHASE_FIGHT : MO_PHASE_PREFIGHT;
}

static bool MO_LocalIntroRunning() {
    BossMo* core = MO_Core();
    return core != nullptr && core->csState != MO_BATTLE && core->csState < MO_DEATH_START;
}

static void MO_SerializeExtras(Actor* actor, nlohmann::json& x) {
    BossMo* mo = (BossMo*)actor;
    x["ac"] = mo->work[MO_TENT_ACTION_STATE];
    x["ba"] = (int)(mo->baseAlpha * 10.0f);
    x["da"] = mo->drawActor;
    x["at"] = (actor->flags & ACTOR_FLAG_ATTENTION_ENABLED) != 0;
    if (!MO_IsTent(actor)) {
        x["hp"] = actor->colChkInfo.health;
        x["hc"] = mo->hitCount;
        x["wl"] = mo->waterLevel;
        x["fl"] = mo->work[MO_CORE_DMG_FLASH_TIMER];
        x["sx"] = (int)(actor->scale.x * 100000.0f);
        x["sy"] = (int)(actor->scale.y * 100000.0f);
        return;
    }
    x["wm"] = (int)(mo->waterLevelMod * 100.0f);
    x["wa"] = (int)mo->waterTexAlpha;
    x["vi"] = mo->anchorVictim;
    x["ci"] = mo->cutIndex;
    x["mi"] = mo->meltIndex;
    x["cs"] = (int)(mo->cutScale * 1000.0f);
    x["nb"] = mo->noBubbles;
    x["lt"] = mo->linkToLeft;
    x["ms"] = (int)(mo->fwork[MO_TENT_MAX_STRETCH] * 1000.0f);
    x["rs"] = (int)(mo->tentRippleSize * 1000.0f);
    x["tp"] = (int)(mo->tentPulse * 1000.0f);
    x["bb"] = mo->baseBubblesTimer;
    x["sp"] = mo->tentSpawnPos;
    // rot x, rot z, stretch y and width x for each of the 41 joints
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < 41; i++) {
        arr.push_back(mo->tentRot[i].x);
        arr.push_back(mo->tentRot[i].z);
        arr.push_back((int)(mo->tentStretch[i].y * 100.0f));
        arr.push_back((int)(mo->tentScale[i].x * 1000.0f));
    }
    x["jt"] = std::move(arr);
}

// This machine's own Link, held by a tentacle the host curled around the player behind it.
static void MO_VictimDriver(BossMo* tent, uint32_t victim, int16_t act) {
    PlayState* play = gPlayState;
    Player* player = GET_PLAYER(play);
    MorphaVictim& v = MO_VictimFor(&tent->actor);
    uint32_t own = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    bool holding = act == MO_TENT_GRAB || act == MO_TENT_SHAKE;
    bool mine = own != 0 && victim == own && (holding || act == MO_TENT_CURL);

    if (!mine || !holding) {
        if (tent->anchorHeld) {
            BossMo_VictimRelease(tent, play, player, v.lastAct == MO_TENT_SHAKE && !v.escaped);
            tent->anchorHeld = false;
            ESYNC_LOG("[MorphaSync] released our Link (act {}, mashed free {})", act, v.escaped);
        }
        if (tent->csCamera != 0 && act != MO_TENT_SHAKE) {
            BossMo_VictimRetreatCamera(tent, play, player);
        }
        if (!mine || act == MO_TENT_RETREAT || act == MO_TENT_CUT) {
            v = MorphaVictim();
            v.tent = &tent->actor;
        }
        v.lastAct = act;
        return;
    }

    if (!tent->anchorHeld) {
        if (player->actor.parent == nullptr && BossMo_VictimGrab(tent, play, player)) {
            tent->anchorHeld = true;
            ESYNC_LOG("[MorphaSync] our Link is held by a tentacle");
        } else {
            if (!v.escaped) {
                v.escaped = true;
                EnemySync::SendAdapterEvent(&tent->actor, MO_EVENT_ESCAPE);
            }
            v.lastAct = act;
            return;
        }
    }
    if (act == MO_TENT_GRAB) {
        BossMo_VictimPin(tent, player, true);
    } else {
        if (!v.shakeStarted) {
            v.shakeStarted = true;
            v.shakeTick = 0;
            BossMo_VictimShakeStart(tent, play);
        }
        if ((v.shakeTick++ % 8) == 0) {
            play->damagePlayer(play, -1);
        }
        u16 pressed = play->state.input[0].press.button;
        if (CHECK_BTN_ALL(pressed, BTN_A) || CHECK_BTN_ALL(pressed, BTN_B)) {
            if (++v.mash == 40 && !v.escaped) {
                v.escaped = true;
                EnemySync::SendAdapterEvent(&tent->actor, MO_EVENT_ESCAPE);
                ESYNC_LOG("[MorphaSync] mashed free, told the host");
            }
        }
        BossMo_VictimPin(tent, player, false);
        BossMo_VictimCamera(tent, play, player);
    }
    v.lastAct = act;
}

// Missing or non-numeric fields (a NaN streams as null) leave the local value alone.
static bool MO_Num(const nlohmann::json& x, const char* k, double& out) {
    auto it = x.find(k);
    if (it == x.end() || !it->is_number()) {
        return false;
    }
    out = it->get<double>();
    return true;
}

static void MO_DeserializeTent(BossMo* mo, const nlohmann::json& x) {
    if (mo != BossMo_AnchorGlobal(1) && mo != BossMo_AnchorGlobal(2)) {
        BossMo_AnchorSetTent2(mo);
    }
    double d;
    if (MO_Num(x, "wm", d)) mo->waterLevelMod = d / 100.0;
    if (MO_Num(x, "wa", d)) mo->waterTexAlpha = (float)d;
    if (MO_Num(x, "ci", d)) mo->cutIndex = (s16)d;
    if (MO_Num(x, "mi", d)) mo->meltIndex = (s16)d;
    if (MO_Num(x, "cs", d)) mo->cutScale = d / 1000.0;
    if (MO_Num(x, "nb", d)) mo->noBubbles = (s16)d;
    if (MO_Num(x, "lt", d)) mo->linkToLeft = (s16)d;
    if (MO_Num(x, "ms", d)) mo->fwork[MO_TENT_MAX_STRETCH] = d / 1000.0;
    if (MO_Num(x, "rs", d)) mo->tentRippleSize = d / 1000.0;
    if (MO_Num(x, "tp", d)) mo->tentPulse = d / 1000.0;
    if (MO_Num(x, "bb", d)) mo->baseBubblesTimer = (s16)d;
    if (MO_Num(x, "sp", d)) mo->tentSpawnPos = (u8)d;
    auto jtIt = x.find("jt");
    if (jtIt != x.end() && jtIt->is_array() && jtIt->size() == 41 * 4) {
        const auto& jt = *jtIt;
        for (int i = 0; i < 41; i++) {
            if (!jt[i * 4].is_number() || !jt[i * 4 + 1].is_number() || !jt[i * 4 + 2].is_number() ||
                !jt[i * 4 + 3].is_number()) {
                continue;
            }
            mo->tentRot[i].x = (s16)jt[i * 4].get<double>();
            mo->tentRot[i].z = (s16)jt[i * 4 + 1].get<double>();
            mo->tentStretch[i].y = jt[i * 4 + 2].get<double>() / 100.0;
            mo->tentScale[i].x = jt[i * 4 + 3].get<double>() / 1000.0;
        }
    }
    uint32_t victim = MO_Num(x, "vi", d) ? (uint32_t)d : 0;
    int16_t act = mo->work[MO_TENT_ACTION_STATE];
    mo->anchorVictim = victim;
    BossMo_TentVisualTick(mo, gPlayState, true);
    MO_VictimDriver(mo, victim, act);
}

static void MO_DeserializeCore(BossMo* mo, const nlohmann::json& x) {
    Actor* actor = &mo->actor;
    double d;
    if (MO_Num(x, "hp", d)) actor->colChkInfo.health = (u8)d;
    if (MO_Num(x, "hc", d)) mo->hitCount = (u8)d;
    if (MO_Num(x, "wl", d)) mo->waterLevel = (f32)d;
    if (MO_Num(x, "fl", d)) mo->work[MO_CORE_DMG_FLASH_TIMER] = (s16)d;
    if (MO_Num(x, "sx", d)) actor->scale.x = actor->scale.z = d / 100000.0;
    if (MO_Num(x, "sy", d)) actor->scale.y = d / 100000.0;
    BossMo_CoreVisualTick(mo, gPlayState);
}

static void MO_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    BossMo* mo = (BossMo*)actor;
    if (gPlayState == NULL || BossMo_AnchorGlobal(1) == nullptr) {
        return;
    }
    double d;
    if (MO_Num(x, "ac", d)) mo->work[MO_TENT_ACTION_STATE] = (s16)d;
    if (MO_Num(x, "ba", d)) mo->baseAlpha = d / 10.0;
    if (MO_Num(x, "da", d)) mo->drawActor = d != 0.0;
    auto at = x.find("at");
    if (at != x.end() && at->is_boolean()) {
        if (at->get<bool>()) {
            actor->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
        } else {
            actor->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
        }
    }
    if (MO_IsTent(actor)) {
        MO_DeserializeTent(mo, x);
    } else {
        MO_DeserializeCore(mo, x);
    }
}

static void MO_StartDefeat() {
    BossMo* core = MO_Core();
    if (core == nullptr || core->csState >= MO_DEATH_START) {
        return;
    }
    BossMo_StartDeath(core, gPlayState);
}

static bool MO_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[MorphaSync] {} phase {}->{}", MO_IsTent(actor) ? "tentacle" : "core", fromPhase, toPhase);
    if (toPhase != MO_PHASE_DEFEATED) {
        return false;
    }
    if (MO_IsTent(actor)) {
        // The core's StartDeath puts tentacle 1 into its death state and times tentacle 2 out.
        BossMo* tent = (BossMo*)actor;
        if (tent->anchorHeld) {
            BossMo_VictimRelease(tent, gPlayState, GET_PLAYER(gPlayState), false);
            tent->anchorHeld = false;
        }
        return true;
    }
    if (MO_LocalIntroRunning()) {
        sPendingDefeat = actor;
        ESYNC_LOG("[MorphaSync] defeat deferred until the local intro ends");
        return true;
    }
    if (fromPhase == MO_PHASE_FIGHT) {
        MO_StartDefeat();
        ESYNC_LOG("[MorphaSync] defeat handoff (BossMo_StartDeath called locally)");
        return true;
    }
    return false;
}

static void MO_OnRemoteDefeat(Actor* actor) {
    if (MO_IsTent(actor)) {
        return;
    }
    BossMo* core = (BossMo*)actor;
    if (core->csState >= MO_DEATH_START) {
        return;
    }
    if (MO_LocalIntroRunning()) {
        sPendingDefeat = actor;
        ESYNC_LOG("[MorphaSync] remote defeat deferred until the local intro ends");
        return;
    }
    MO_StartDefeat();
    ESYNC_LOG("[MorphaSync] remote defeat (missed phase edge, BossMo_StartDeath called locally)");
}

static bool MO_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    BossMo* core = MO_Core();
    return streamedPhase == MO_PHASE_FIGHT && core != nullptr && core->csState == MO_BATTLE;
}

static void MO_OnLocalResume(Actor* actor) {
    BossMo* mo = (BossMo*)actor;
    if (MO_IsTent(actor)) {
        if (mo->anchorHeld) {
            BossMo_VictimRelease(mo, gPlayState, GET_PLAYER(gPlayState), false);
            mo->anchorHeld = false;
        }
        mo->anchorVictim = 0;
        int16_t act = mo->work[MO_TENT_ACTION_STATE];
        if (act == MO_TENT_CURL || act == MO_TENT_GRAB || act == MO_TENT_SHAKE) {
            mo->work[MO_TENT_ACTION_STATE] = MO_TENT_RETREAT;
            mo->timers[0] = 75;
        }
    } else if (mo->csState == MO_BATTLE) {
        mo->timers[0] = 30;
    }
    ESYNC_LOG("[MorphaSync] local AI resumes ({})", MO_IsTent(actor) ? "tentacle" : "core");
}

static void MO_OnRemoteEvent(Actor* actor, uint8_t event) {
    BossMo* tent = (BossMo*)actor;
    if (event != MO_EVENT_ESCAPE || !MO_IsTent(actor)) {
        return;
    }
    // The held player's machine counted 40 mashes (or could not take the grab): let go at the next swing.
    if (tent->anchorVictim != 0 && tent->work[MO_TENT_ACTION_STATE] == MO_TENT_SHAKE) {
        tent->mashCounter = 40;
        ESYNC_LOG("[MorphaSync] victim escaped the tentacle");
    } else if (tent->anchorVictim != 0 && tent->work[MO_TENT_ACTION_STATE] == MO_TENT_GRAB) {
        tent->timers[0] = 0;
    }
}

void RegisterMorphaAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = MO_SerializeExtras;
    adapter.DeserializeExtras = MO_DeserializeExtras;
    adapter.GetPhase = MO_GetPhase;
    adapter.OnPhaseChange = MO_OnPhaseChange;
    adapter.ShouldMirror = MO_ShouldMirror;
    adapter.OnRemoteDefeat = MO_OnRemoteDefeat;
    adapter.OnLocalResume = MO_OnLocalResume;
    adapter.OnRemoteEvent = MO_OnRemoteEvent;
    EnemySync::RegisterAdapter(ACTOR_BOSS_MO, adapter);
}

// z_boss_mo.c, at the end of the intro: the partner won while our intro played.
extern "C" s32 Anchor_MorphaDefeatPending(Actor* actor) {
    if (sPendingDefeat != actor) {
        return false;
    }
    sPendingDefeat = nullptr;
    ESYNC_LOG("[MorphaSync] local intro over: starting the deferred defeat");
    return true;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4051 boss rig. Reports Morpha's sync state on this client; cmd 0 report only; 1 land a Kokiri Sword hit
// on the core the way the collision check would; 2 (host) make tentacle 1 swing now; 3 (host) set the core's
// health to arg; 4 un-clear this room (so she is back on the next visit); 5 (host) park the core in the
// stunned state so a hit lands. Every call returns the report.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_mo(int cmd, int arg) {
    static std::string out;
    static ColliderInfo sToucher;
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
    j["held"] = player->actor.parent != nullptr;
    j["csAction"] = player->csAction;
    j["iframes"] = player->invincibilityTimer;
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
    if (cmd == 4) {
        Flags_UnsetClear(gPlayState, gPlayState->roomCtx.curRoom.num);
    }
    BossMo* core = BossMo_AnchorGlobal(0);
    j["present"] = core != nullptr;
    if (core == nullptr) {
        out = j.dump();
        return out.c_str();
    }
    bool sup = EnemySync::IsSuppressed(&core->actor);
    if (cmd == 1 || cmd == 6) {
        memset(&sToucher, 0, sizeof(sToucher));
        sToucher.toucher.dmgFlags = cmd == 6 ? DMG_HOOKSHOT : DMG_SLASH_KOKIRI;
        sToucher.toucher.damage = cmd == 6 ? 0 : 1;
        sToucher.toucherFlags = TOUCH_ON | TOUCH_HIT;
        core->coreCollider.base.acFlags |= AC_HIT;
        core->coreCollider.base.ac = &player->actor;
        core->coreCollider.info.bumperFlags |= BUMP_HIT;
        core->coreCollider.info.acHitInfo = &sToucher;
        core->actor.colChkInfo.damage = 1;
    } else if (cmd == 2 && !sup) {
        BossMo* t = BossMo_AnchorGlobal(1);
        if (t != nullptr && (t->work[MO_TENT_ACTION_STATE] == MO_TENT_READY || t->work[MO_TENT_ACTION_STATE] == MO_TENT_SWING)) {
            t->work[MO_TENT_ACTION_STATE] = MO_TENT_SWING;
            t->timers[0] = 0;
        }
    } else if (cmd == 3 && !sup) {
        core->actor.colChkInfo.health = arg;
    } else if (cmd == 5 && !sup && core->csState == MO_BATTLE) {
        core->work[MO_TENT_ACTION_STATE] = MO_CORE_STUNNED;
        core->timers[0] = 200;
        core->work[MO_TENT_INVINC_TIMER] = 0;
    }
    j["phase"] = MO_GetPhase(&core->actor);
    j["cs"] = core->csState;
    j["act"] = core->work[MO_TENT_ACTION_STATE];
    j["hp"] = core->actor.colChkInfo.health;
    j["hits"] = core->hitCount;
    j["sup"] = sup;
    j["dying"] = EnemySync::IsDying(&core->actor);
    j["key"] = std::to_string(EnemySync::KeyForActor(&core->actor));
    j["pos"] = { core->actor.world.pos.x, core->actor.world.pos.y, core->actor.world.pos.z };
    j["water"] = gPlayState->colCtx.colHeader != nullptr ? gPlayState->colCtx.colHeader->waterBoxes[0].ySurface : 0;
    j["pending"] = sPendingDefeat == &core->actor;
    nlohmann::json tents = nlohmann::json::array();
    for (int i = 1; i <= 2; i++) {
        BossMo* t = BossMo_AnchorGlobal(i);
        if (t == nullptr) {
            continue;
        }
        nlohmann::json e;
        e["n"] = i;
        e["act"] = t->work[MO_TENT_ACTION_STATE];
        e["victim"] = t->anchorVictim;
        e["held"] = t->anchorHeld;
        e["cut"] = t->cutIndex;
        e["sup"] = EnemySync::IsSuppressed(&t->actor);
        e["alpha"] = t->baseAlpha;
        e["draw"] = t->drawActor;
        e["key"] = std::to_string(EnemySync::KeyForActor(&t->actor));
        e["pos"] = { t->actor.world.pos.x, t->actor.world.pos.y, t->actor.world.pos.z };
        e["tip"] = { t->tentTipPos.x, t->tentTipPos.y, t->tentTipPos.z };
        tents.push_back(e);
    }
    j["tents"] = tents;
    out = j.dump();
    return out.c_str();
}
}
#endif

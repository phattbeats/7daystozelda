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
#include "src/overlays/actors/ovl_Boss_Goma/z_boss_goma.h"

void BossGoma_SetupDefeated(BossGoma* thisx, PlayState* play);
void BossGoma_SetupEncounterState4(BossGoma* thisx, PlayState* play);
extern PlayState* gPlayState;
}

/**
 * Gohma (M3 vertical slice).
 *
 * State-gated suppression: the intro cutscene IS Gohma's Update, so both
 * clients run their own intro locally (harmless — while disableGameplayLogic
 * is set, Gohma submits no colliders and applies no gameplay). Mirroring
 * engages only while the streamed phase is FIGHT and the local intro has
 * finished. On the FIGHT -> DEFEATED edge the mirror calls
 * BossGoma_SetupDefeated directly (plain un-suppression would leave a stale
 * actionFunc) and hands the defeat cutscene to the local simulation — both
 * clients get the cutscene, heart container, blue warp, and room-clear flag
 * natively (all deduped by stock Anchor sync).
 */

enum GohmaPhase : uint8_t {
    GOHMA_PHASE_PREFIGHT = 0,
    GOHMA_PHASE_FIGHT = 1,
    GOHMA_PHASE_DEFEATED = 2,
};

static uint8_t Gohma_GetPhase(Actor* actor) {
    BossGoma* goma = (BossGoma*)actor;
    if (goma->disableGameplayLogic) {
        return ((int8_t)actor->colChkInfo.health <= 0) ? GOHMA_PHASE_DEFEATED : GOHMA_PHASE_PREFIGHT;
    }
    return GOHMA_PHASE_FIGHT;
}

static void Gohma_SerializeExtras(Actor* actor, nlohmann::json& x) {
    BossGoma* g = (BossGoma*)actor;
    x["eye"] = g->eyeState;
    x["irx"] = g->eyeIrisRotX;
    x["iry"] = g->eyeIrisRotY;
    x["lidB"] = g->eyeLidBottomRotX;
    x["lidT"] = g->eyeLidTopRotX;
    x["isx"] = g->eyeIrisScaleX;
    x["isy"] = g->eyeIrisScaleY;
    x["vis"] = g->visualState;
    x["inv"] = g->invincibilityFrames;
    x["menv"] = { g->mainEnvColor[0], g->mainEnvColor[1], g->mainEnvColor[2] };
    x["eenv"] = { g->eyeEnvColor[0], g->eyeEnvColor[1], g->eyeEnvColor[2] };
    x["tail"] = { g->tailLimbsScale[0], g->tailLimbsScale[1], g->tailLimbsScale[2], g->tailLimbsScale[3] };
    // deadLimbsState / decayingProgress / noBackfaceCulling deliberately not
    // streamed: the defeat sequence runs locally on both clients, and streaming
    // deadLimbsState would double-spawn the PostLimbDraw limb pieces.
}

static void Gohma_DeserializeExtras(Actor* actor, const nlohmann::json& x) {
    BossGoma* g = (BossGoma*)actor;
    try {
        if (x.contains("eye")) g->eyeState = x["eye"].get<int16_t>();
        if (x.contains("irx")) g->eyeIrisRotX = x["irx"].get<int16_t>();
        if (x.contains("iry")) g->eyeIrisRotY = x["iry"].get<int16_t>();
        if (x.contains("lidB")) g->eyeLidBottomRotX = x["lidB"].get<int16_t>();
        if (x.contains("lidT")) g->eyeLidTopRotX = x["lidT"].get<int16_t>();
        if (x.contains("isx")) g->eyeIrisScaleX = x["isx"].get<float>();
        if (x.contains("isy")) g->eyeIrisScaleY = x["isy"].get<float>();
        if (x.contains("vis")) g->visualState = x["vis"].get<int16_t>();
        if (x.contains("inv")) g->invincibilityFrames = x["inv"].get<int16_t>();
        if (x.contains("menv") && x["menv"].size() == 3) {
            for (int i = 0; i < 3; i++) g->mainEnvColor[i] = x["menv"][i].get<float>();
        }
        if (x.contains("eenv") && x["eenv"].size() == 3) {
            for (int i = 0; i < 3; i++) g->eyeEnvColor[i] = x["eenv"][i].get<float>();
        }
        if (x.contains("tail") && x["tail"].size() == 4) {
            for (int i = 0; i < 4; i++) g->tailLimbsScale[i] = x["tail"][i].get<float>();
        }
    } catch (const std::exception& ex) {
        SPDLOG_WARN("[GohmaSync] extras parse error: {}", ex.what());
    }
}

static bool Gohma_OnPhaseChange(Actor* actor, uint8_t fromPhase, uint8_t toPhase) {
    ESYNC_LOG("[GohmaSync] phase {}->{}", fromPhase, toPhase);
    if (toPhase == GOHMA_PHASE_DEFEATED && fromPhase == GOHMA_PHASE_FIGHT) {
        // Final streamed pose is already applied; run the defeat locally from
        // here (cutscene, decay, heart container, blue warp, Flags_SetClear).
        actor->colChkInfo.health = 0;
        BossGoma_SetupDefeated((BossGoma*)actor, gPlayState);
        ESYNC_LOG("[GohmaSync] defeat handoff (SetupDefeated called locally)");
        return true;
    }
    return false;
}

static void Gohma_OnRemoteDefeat(Actor* actor) {
    if (Gohma_GetPhase(actor) == GOHMA_PHASE_DEFEATED) {
        return; // already decaying locally
    }
    actor->colChkInfo.health = 0;
    BossGoma_SetupDefeated((BossGoma*)actor, gPlayState);
    ESYNC_LOG("[GohmaSync] remote defeat (missed phase edge, SetupDefeated called locally)");
}

static bool Gohma_ShouldMirror(Actor* actor, uint8_t streamedPhase) {
    BossGoma* g = (BossGoma*)actor;
    // Mirror only during the fight proper, and only once our own intro is done
    // (the intro cutscene must run locally on every client).
    return streamedPhase == GOHMA_PHASE_FIGHT && !g->disableGameplayLogic;
}

void RegisterGohmaAdapter() {
    ActorSyncAdapter adapter;
    adapter.SerializeExtras = Gohma_SerializeExtras;
    adapter.DeserializeExtras = Gohma_DeserializeExtras;
    adapter.GetPhase = Gohma_GetPhase;
    adapter.OnPhaseChange = Gohma_OnPhaseChange;
    adapter.ShouldMirror = Gohma_ShouldMirror;
    adapter.OnRemoteDefeat = Gohma_OnRemoteDefeat;
    EnemySync::RegisterAdapter(ACTOR_BOSS_GOMA, adapter);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" {
// #4046 rig. Reports Gohma's sync state and every Door_Warp1 / Item_B_Heart position
// on this client. cmd 1: start the defeat sequence here (host), as the killing blow does;
// cmd 3: stand it near the room's centre (arg = x offset), where the warp spot is re-rolled;
// cmd 2: end the intro's wait for the player to look at Gohma.
EMSCRIPTEN_KEEPALIVE
const char* anchor_test_gm(int cmd, int arg) {
    static std::string out;
    nlohmann::json j;
    if (gPlayState == NULL) {
        return "{}";
    }
    BossGoma* goma = nullptr;
    for (Actor* a = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head; a != nullptr; a = a->next) {
        if (a->id == ACTOR_BOSS_GOMA && a->update != NULL) {
            goma = (BossGoma*)a;
            break;
        }
    }
    Player* player = GET_PLAYER(gPlayState);
    j["scene"] = gPlayState->sceneNum;
    j["auth"] = EnemySync::CurrentAuthorityId();
    j["own"] = Anchor::Instance != nullptr ? Anchor::Instance->ownClientId : 0;
    j["link"] = { player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z };
    nlohmann::json warps = nlohmann::json::array(), hearts = nlohmann::json::array();
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = gPlayState->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            if (a->id == ACTOR_DOOR_WARP1 && a->update != NULL) {
                warps.push_back({ a->world.pos.x, a->world.pos.y, a->world.pos.z });
            } else if (a->id == ACTOR_ITEM_B_HEART && a->update != NULL) {
                hearts.push_back({ a->world.pos.x, a->world.pos.y, a->world.pos.z });
            }
        }
    }
    j["warps"] = warps;
    j["hearts"] = hearts;
    j["present"] = goma != nullptr;
    if (goma != nullptr) {
        Actor* actor = &goma->actor;
        j["phase"] = Gohma_GetPhase(actor);
        j["hp"] = (int)actor->colChkInfo.health;
        j["pos"] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
        j["sup"] = EnemySync::IsSuppressed(actor);
        j["dying"] = EnemySync::IsDying(actor);
        j["as"] = goma->actionState;
        j["tick"] = (unsigned)gPlayState->gameplayFrames;
        if (cmd == 2 && goma->actionState == 3) {
            BossGoma_SetupEncounterState4(goma, gPlayState); // the player "looked at Gohma": ends the intro wait
        }
        if (cmd == 3 && !EnemySync::IsSuppressed(actor)) {
            actor->world.pos.x = -150.0f + arg;
            actor->world.pos.z = -350.0f;
        }
        if (cmd == 1 && !EnemySync::IsSuppressed(actor)) {
            actor->colChkInfo.health = 0;
            BossGoma_SetupDefeated(goma, gPlayState);
        }
    }
    out = j.dump();
    return out.c_str();
}
}
#endif
